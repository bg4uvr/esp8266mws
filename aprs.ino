// ESP8266 APRS 气象站

#define DEBUG_MODE //调试模式时不把语句发往服务器

//调试用宏语句
//在调试主机已连接时，将把相关消息发往主机
#define DBGPRINT(x)             \
    if (client_dbg.connected()) \
        client_dbg.print(x);

#define DBGPRINTLN(x)           \
    if (client_dbg.connected()) \
        client_dbg.println(x);

#include <ESP8266WiFi.h>
#include <Wire.h>
#include <Time.h>
#include <WiFiManager.h>
#include <Adafruit_BMP280.h>
#include <DHTesp.h>
#include <EEPROM.h>
#include <ArduinoOTA.h>

//系统状态
enum sys_mode_t
{
    sys_FAIL,      //非法状态
    sys_CFG,       //配置状态
    sys_RUN,       //正常状态
    sys_SLEEP,     //休眠状态
    sys_RUN2SLEEP, //正常转为休眠状态
    sys_SLEEP2RUN, //休眠转为正常状态
};

//配置数据
struct cfg_t
{
    char aprs_server_addr[26];  //APRS服务器地址 26
    uint16_t aprs_server_port;  //APRS服务器端口 2
    char debug_server_addr[26]; //调试主机地址 26
    uint16_t debug_server_port; //调试主机端口 2
    char callsign[8];           //呼号 6
    int ssid;                   //SSID 2
    uint16_t password;          //APRS密码 2
    uint16_t min_send_interval; //最小发送间隔 2
    uint16_t max_send_interval; //最大发送间隔 2
    float suspend_voltage;      //休眠电压 4
    float lon;                  //经度 4
    float lat;                  //纬度 4
    sys_mode_t sysmode;         //系统状态 4
    uint32_t crc;               //校验值 4
};

//全局变量
cfg_t mycfg;                        //系统配置参数
WiFiClient client_aprs, client_dbg; //实例化aprs服务器连接和调试连接
float voltage;                      //电池电压
uint16_t sleepsec;                  //下次工作延时
uint32_t last_send;                 //上一次发送时刻
char msgbuf[150] = {0};             //消息格式化缓存

//crc32
uint32_t crc32(uint8_t *data, int length)
{
    uint32_t crc = 0xffffffff;
    uint8_t *ldata = data;
    while (length--)
    {
        uint8_t c = *(ldata++);
        for (uint32_t i = 0x80; i > 0; i >>= 1)
        {
            bool bit = crc & 0x80000000;
            if (c & i)
                bit = !bit;
            crc <<= 1;
            if (bit)
                crc ^= 0x04c11db7;
        }
    }
    return crc;
}

//自动配网
void WiFisetup()
{
    WiFiManager wifiManager;
    //wifiManager.resetSettings();
    wifiManager.setConfigPortalTimeout(300);

    if (!wifiManager.autoConnect("Esp8266MWS-SET"))
    {
        delay(3000);
        ESP.reset();
        //重置并重试
        delay(5000);
    }
}

//读取BMP280
bool read_bmp280(float *temperature, float *pressure)
{
    Adafruit_BMP280 bmp; //初始化BMP280实例
    DBGPRINTLN("正在初始化BMP280传感器...");
    Wire.begin(0, 5); //重定义I2C端口
    if (!bmp.begin(BMP280_ADDRESS_ALT))
    {
        return false;
    }
    DBGPRINTLN("BMP280传感器初始化成功");

    //设置BMP280采样参数
    bmp.setSampling(Adafruit_BMP280::MODE_FORCED, //FORCE模式读完自动转换回sleep模式？
                    Adafruit_BMP280::SAMPLING_X1,
                    Adafruit_BMP280::SAMPLING_X4,
                    Adafruit_BMP280::FILTER_X16,
                    Adafruit_BMP280::STANDBY_MS_1);
    DBGPRINTLN("正在读取BMP280传感器");
    *temperature = bmp.readTemperature();
    *pressure = bmp.readPressure();
    DBGPRINTLN("BMP280读取完成");
    return true;
}

//读取DHT11
bool read_dht11(float *humidity)
{
    DHTesp dht; //DHT11实例
    DBGPRINTLN("正在初始化DHT11传感器...");
    dht.setup(14, DHTesp::DHT11); // Connect DHT sensor to GPIO5
    DBGPRINTLN("正在读取DHT11传感器");
    *humidity = dht.getHumidity();

    if (dht.getStatus() == dht.ERROR_NONE)
    {
        DBGPRINTLN("DHT11读取成功");
        return true;
    }
    else
        return false;
}

//发送数据
void send_data()
{
    float humidity, temperatureBMP, pressure;   //保存湿度、温度、气压的浮点变量
    int humidityINT, temperatureF, pressureINT; //保存湿度、华氏温度、气压的整数变量

    bool bmpRES = read_bmp280(&temperatureBMP, &pressure); //读取BMP280
    bool dhtRES = read_dht11(&humidity);                   //读取DHT11湿度

    if (dhtRES) //DHT11读取成功
    {
        if (client_dbg.connected())
        {
            snprintf(msgbuf, sizeof(msgbuf), "DHT11湿度：%0.2f", humidity);
            client_dbg.println(msgbuf);
        }
        humidityINT = humidity; //湿度转换为整数
        if (humidityINT > 100)  //限制范围1-99
            humidityINT = 99;
        if (humidity < 0)
            humidityINT = 1;
    }
    else //DHT11读取失败
    {
        DBGPRINTLN("DHT11读取失败");
        humidityINT = 50; //湿度值设为50
    }

    if (bmpRES) //BMP280读取成功
    {
        snprintf(msgbuf, sizeof(msgbuf), "BMP280温度：%0.2f\tBMP280气压%0.2f", temperatureBMP, pressure);
        DBGPRINTLN(msgbuf);

        temperatureF = temperatureBMP * 9 / 5 + 32; //转换成华氏度
        pressureINT = pressure / 10;                //把气压浮点数的Pa值转换成0.1hPa的整形数值
    }
    else //BMP280读取失败
    {
        DBGPRINTLN("BMP280读取失败");
        temperatureF = 25;   //温度值设置为25度
        pressureINT = 10132; //气压值设置为10132
    }

    if (mycfg.sysmode == sys_RUN) //3153.47N/12106.86E
        snprintf(msgbuf, sizeof(msgbuf),
                 "BG4UVR-10>APZUVR,qAC,:=%0.2f%c/%0.2f%c_c000s000g000t%03dr000p000h%02db%05d battery:%0.2fV, interval: %dmins",
                 mycfg.lat, mycfg.lat > 0 ? 'N' : 'S', mycfg.lon, mycfg.lon > 0 ? 'E' : 'W', temperatureF, humidityINT, pressureINT, voltage, sleepsec / 60);
    else if (mycfg.sysmode == sys_RUN2SLEEP)
        snprintf(msgbuf, sizeof(msgbuf),
                 "BG4UVR-10>APZUVR,qAC,:=%0.2f%c/%0.2f%c_c000s000g000t%03dr000p000h%02db%05d BATTTERY TOO LOW, SYSTEM　SUSPENDED",
                 mycfg.lat, mycfg.lat > 0 ? 'N' : 'S', mycfg.lon, mycfg.lon > 0 ? 'E' : 'W', temperatureF, humidityINT, pressureINT);

    DBGPRINTLN(msgbuf);

#ifndef DEBUG_MODE
    client_aprs.println(msgbuf); //数据发往服务器
#endif
}

//连接APRS服务器
bool loginAPRS()
{
    uint8_t retrycnt = 0;
    DBGPRINTLN("正在连接APRS服务器");
    do
    {
        if (client_aprs.connect(mycfg.aprs_server_addr, mycfg.aprs_server_port))
        {
            DBGPRINTLN("APRS服务器已连接");
            do
            {
                uint8_t recv_cnt = 0;
                if (client_aprs.available()) //如果缓冲区字符串大于0
                {
                    String line = client_aprs.readStringUntil('\n'); //获取字符串
                    DBGPRINTLN(line);                                //把字符串传给串口

                    if (line.indexOf("aprsc") != -1) //语句含有 aprsc 并且未登录时
                    {
                        DBGPRINTLN("正在登录ARPS服务器...");
                        sprintf(msgbuf, "user %s-%d pass %d vers Esp8266MWS 0.1 filter m/10", mycfg.callsign, mycfg.ssid, mycfg.password);
                        //String loginmsg = "user BG4UVR-13 pass 21410 vers Esp8266WeatherStation 0.1 filter m/10"; //APRS登录命令
                        client_aprs.println(msgbuf); //发送登录语句
                        DBGPRINTLN(msgbuf);
                    }
                    else if (line.indexOf("verified") != -1)
                    {
                        DBGPRINTLN("APRS服务器登录成功");
                        send_data(); //发送数据
                        DBGPRINTLN("本次数据发送已完成");
                        return true;
                    }
                    else if (line.indexOf("# Server full") != -1 || line.indexOf("# Port full") != -1)
                    {
                        DBGPRINTLN("服务器已负荷已满，稍后重试");
                        return false;
                    }
                    if (++recv_cnt > 5)
                    {
                        DBGPRINTLN("错误：没能从服务器接收到预期类型的数据");
                        return false;
                    };
                }
                else
                    delay(2000);
            } while (++retrycnt < 5); //10秒内未收到数据认为超时
            DBGPRINTLN("错误：接收APRS服务器数据超时");
            return false;
        }
        else
        {
            DBGPRINT('.');
            delay(1000);
        }
    } while (++retrycnt < 10);
    DBGPRINTLN("\n错误：连接APRS失败次数过多，已退出重试");
    return false;
}

//电压过低判断处理
void voltageLOW()
{
#ifndef DEBUG_MODE
    voltage = 4.11f * analogRead(A0) / 1024; //读取并计算电池电压
    if (voltage < 3.0f)                      //如果电压小于3.0V，直接休眠60分钟
    {
        digitalWrite(LED_BUILTIN, 1); //关灯
        ESP.deepSleep((uint32_t)60 * 60 * 1000 * 1000);
    }
#else
    voltage = 4.0f;
#endif
}

//显示帮助信息
void disphelpmsg()
{
    client_dbg.println("\n\t《Esp8266MWS》 Esp8266 Mini Weather Station");
    client_dbg.println("当前系统未配置或配置数据已损坏，请发送配置命令。");
    client_dbg.println("命令格式：");
    client_dbg.println("    SET:APRS_ADDR,APRS_PORT,DEBUG_ADDR,DEBUG_PORT,CALLSIGN,SSID,PASSWWORD,MIN_I,MAX_I,SUSPEND_V,LON,LAT*");
    client_dbg.println("");
    client_dbg.println("    APRS_ADDR:  APRS服务器地址，可以是域名也可以是IP地址");
    client_dbg.println("    APRS_PORT:  APRS服务器端口号");
    client_dbg.println("    DEBUG_ADD:  调试配置服务器地址，需要设置为配件用电脑局域网的IP地址");
    client_dbg.println("    DEBUG_PORT: 调试配置服端口号");
    client_dbg.println("    CALLSIGN:   呼号，最多可以6位数字和字母");
    client_dbg.println("    SSID:       辅助ID号（建议值:13）");
    client_dbg.println("    MIN_I:      最小数据发送间隔，单位为“秒”，电池电压最高时以此间隔发送数据（建议值：300）");
    client_dbg.println("    MAX_I:      最大数据发送间隔，单位为“秒”，电池电压最低时以此间隔发送数据（建议值：1800）");
    client_dbg.println("    SUSPEND_V:  保护休眠电压，单位为“伏”，电压低于此值时系统将停止工作（设置范围：3.0-3.6）");
    client_dbg.println("    LON:        台站经度，格式：dddmm.mm，东经为正，西经为负");
    client_dbg.println("    LAT:        台站纬度，格式：ddmm.mm，北纬为正，南纬为负");
    client_dbg.println("");
    client_dbg.println("配置命令示例:");
    client_dbg.println("    SET china.aprs2.net 14580 192.168.1.125 2222 BGnXXX 13 12345 300 1800 3.4 12100.00 3200.00*");
}

void dispset(cfg_t *c)
{
    client_dbg.print("aprs服务器地址:\t");
    client_dbg.println(c->aprs_server_addr);
    client_dbg.print("aprs服务器端口:\t");
    client_dbg.println(c->aprs_server_port);
    client_dbg.print("调试主机地址:\t");
    client_dbg.println(c->debug_server_addr);
    client_dbg.print("调试主机端口:\t");
    client_dbg.println(c->debug_server_port);
    client_dbg.print("呼号:\t");
    client_dbg.println(c->callsign);
    client_dbg.print("SSID:\t");
    client_dbg.println(c->ssid);
    client_dbg.print("APRS验证码:\t");
    client_dbg.println(c->password);
    client_dbg.print("最小发送间隔:\t");
    client_dbg.println(c->min_send_interval);
    client_dbg.print("最大发送间隔:\t");
    client_dbg.println(c->max_send_interval);
    client_dbg.print("休眠电压:\t");
    client_dbg.println(c->suspend_voltage);
    client_dbg.print("经度:\t");
    client_dbg.println(c->lon);
    client_dbg.print("纬度:\t");
    client_dbg.println(c->lat);
}

//系统配置数据设置子程序
void set_cfg()
{
    String line = client_dbg.readStringUntil('\n'); //获取字符串
    client_dbg.println("\n您发送的命令为：");       //命令回显
    client_dbg.println(line);

    int16_t head, comma, tail; //查找是否包含命令头，分隔符和命令尾
    head = line.indexOf("SET");
    tail = line.indexOf('*');

    //准备解析并判断配置指令
    if (head != -1 && tail != -1) //语句含有 SET: 和 逗号 和 * 号
    {
        //解析命令字符串
        //SET china.aprs2.net 14580 192.168.1.125 2222 BGnXXX 13 300 1800 3.4 12100.00 3200.00*
        char *buf = new char[line.length() + 1]; //新建缓存，用于String类型转换为char[]类型
        strcpy(buf, line.c_str());               //复制字符串

        cfg_t read_cfg; //新建配置字变量
        char *p;        //新建用于分割字符串的指针

        //分割，第一处"SET "丢弃
        p = strtok(buf, " ");
        if (p)
        {
            p = strtok(NULL, " ");
            //再次分割
            if (p)
            {
                strcpy(read_cfg.aprs_server_addr, p);
                p = strtok(NULL, " ");
                if (p)
                {
                    read_cfg.aprs_server_port = atoi(p);
                    p = strtok(NULL, " ");
                    if (p)
                    {
                        strcpy(read_cfg.debug_server_addr, p);
                        p = strtok(NULL, " ");
                        if (p)
                        {
                            read_cfg.debug_server_port = atoi(p);
                            p = strtok(NULL, " ");
                            if (p)
                            {
                                strcpy(read_cfg.callsign, p);
                                p = strtok(NULL, " ");
                                if (p)
                                {
                                    read_cfg.ssid = atoi(p);
                                    p = strtok(NULL, " ");
                                    if (p)
                                    {
                                        read_cfg.password = atoi(p);
                                        p = strtok(NULL, " ");
                                        if (p)
                                        {
                                            read_cfg.min_send_interval = atoi(p);
                                            p = strtok(NULL, " ");
                                            if (p)
                                            {
                                                read_cfg.max_send_interval = atoi(p);
                                                p = strtok(NULL, " ");
                                                if (p)
                                                {
                                                    read_cfg.suspend_voltage = atof(p);
                                                    p = strtok(NULL, " ");
                                                    if (p)
                                                    {
                                                        read_cfg.lon = atof(p);
                                                        p = strtok(NULL, " ");
                                                        if (p)
                                                            read_cfg.lat = atof(p);
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        delete[] buf;

        client_dbg.print("\n命令解析结果：\n");
        dispset(&read_cfg);

        //配置数据存在明显错误
        if (
            read_cfg.ssid < 0 ||
            read_cfg.ssid > 15 ||
            read_cfg.max_send_interval > 3600 ||
            read_cfg.min_send_interval < 60 ||
            read_cfg.suspend_voltage < 3.0 ||
            read_cfg.suspend_voltage > 3.6 ||
            read_cfg.lon < -18000 ||
            read_cfg.lon > 18000 ||
            read_cfg.lat < -9000 ||
            read_cfg.lat > 9000)

            client_dbg.println("您设置的参数可能有误，请仔细检查修正后重试");

        //配置数据通过基本的检测
        else
        {
            mycfg = read_cfg;                                        //保存配置数据
            mycfg.sysmode = sys_RUN;                                 //更改系统状态为运行状态
            mycfg.crc = crc32((uint8_t *)&mycfg, sizeof(mycfg) - 4); //计算配置数据校验值
            for (uint8_t i = 0; i < sizeof(cfg_t); i++)              //写入配置数据
                EEPROM.write(i, ((uint8_t *)&mycfg)[i]);
            EEPROM.commit(); //提交数据
            EEPROM.end();    //写入FLASH
            client_dbg.println("设置参数已成功保存");
            client_dbg.println("请注意：此处无法准确检查您所设定参数的正确性，您需要根据运行结果来自行确认！");
        }
    }
    else
    {
        client_dbg.println("错误：你发送的命令格式不正确，请更正后重试！");
    }
}

//系统初始化
void setup()
{
    //配置指示灯并点亮
    pinMode(2, OUTPUT);
    digitalWrite(LED_BUILTIN, 0);

    //电压低判断（电压过低时会直接重新进入休眠60分钟）
    voltageLOW();

    //电压未报警，往下运行
    WiFisetup();                              //自动配网
    ArduinoOTA.setHostname("Esp8266MWS-OTA"); //设置OTA主机名
    ArduinoOTA.begin();                       //初始化OTA
    EEPROM.begin(128);                        //初始化EEPROM

    //读取系统配置数据
    for (uint8_t i = 0; i < sizeof(cfg_t); i++)
        *((uint8_t *)&mycfg + i) = EEPROM.read(i);

    //校验配置数据（如果校验失败，进入配置模式）
    if (crc32((uint8_t *)&mycfg, sizeof(mycfg) - 4) != mycfg.crc)
        mycfg.sysmode = sys_CFG;

    //校验成功后，根据当前电压状态来确定系统运行模式
    else
    {
        //如果电压正常
        if (voltage >= mycfg.suspend_voltage)
        {
            if (mycfg.sysmode != sys_RUN)      //如果不是运行状态
                mycfg.sysmode = sys_SLEEP2RUN; //设置状态为“休眠 -> 运行”状态
        }

        //如果电压低于设定值
        else if (mycfg.sysmode == sys_RUN) //并且还是是运行状态
            mycfg.sysmode = sys_SLEEP2RUN; //设置状态为“运行 -> 休眠”状态
    }
}

//程序主循环
void loop()
{
    //系统运行模式转换处理
    switch (mycfg.sysmode)
    {
        //休眠转正常
    case sys_SLEEP2RUN:
        mycfg.sysmode = sys_RUN;                                 //设置为运行模式
        mycfg.crc = crc32((uint8_t *)&mycfg, sizeof(mycfg) - 4); //计算校验值
        for (uint8_t i = 0; i < sizeof(cfg_t); i++)              //写入配置数据
            EEPROM.write(i, ((uint8_t *)&mycfg)[i]);
        EEPROM.commit(); //提交数据
        EEPROM.end();    //写入FLASH

        //正常状态
    case sys_RUN:
        sleepsec = 300 + (4.2f - voltage) * 1500 / (4.2f - mycfg.suspend_voltage); //计算休眠时间，均匀延时（30分-5分)
        break;

        //正常转休眠
    case sys_RUN2SLEEP:
        mycfg.sysmode = sys_SLEEP;                               //设置为运行模式
        mycfg.crc = crc32((uint8_t *)&mycfg, sizeof(mycfg) - 4); //计算校验值
        for (uint8_t i = 0; i < sizeof(cfg_t); i++)              //写入配置数据
            EEPROM.write(i, ((uint8_t *)&mycfg)[i]);
        EEPROM.commit(); //提交数据
        EEPROM.end();    //写入FLASH

        //休眠状态
    case sys_SLEEP:
        sleepsec = 60 * 60; //休眠60分钟
        break;

        //非法状态以及其他任何不确定的状态，都进入配置模式
    case sys_FAIL:
    default:
        mycfg.sysmode = sys_CFG;
        break;
    }

    //数据处理
    //配置状态
    if (mycfg.sysmode == sys_CFG)
    {
        //除非电压过低，不然一直尝试连接默认配置服务器地址
        while (!client_dbg.connect("192.168.1.125", 14580))
        {
            voltageLOW();        //电压过低判断
            ArduinoOTA.handle(); //OTA处理
            delay(2000);         //延时2秒
        }

        //已连接到默认配置服务器
        client_dbg.print("\n当前设备IP地址为：");
        client_dbg.println(WiFi.localIP()); //显示设备当前局域网IP地址

        dispset(&mycfg); //显示当前配置
        disphelpmsg();   //发送配置提示信息

        //此处解析配置命令，直到配置成功转为运行状态
        while (client_dbg.connected() && mycfg.sysmode != sys_RUN)
        {
            //如果调试服务器是有数据发来，处理配置数据
            if (client_dbg.available())
                set_cfg();

            voltageLOW();        //电压过低判断
            ArduinoOTA.handle(); //OTA处理
            delay(10);           //延时
        }
    } //初始配置模式

    //除配置状态外的工作状态
    else
    {
        //如果连接到调试服务器成功
        if (client_dbg.connect(mycfg.debug_server_addr, mycfg.debug_server_port))
        {
            //只要服务器连接未断开就一直循环运行
            while (client_dbg.connected())
            {
                //登录APRS服务器并发送数据
                if (loginAPRS())
                    sleepsec = 300 + (4.2f - voltage) * 1500 / (4.2f - mycfg.suspend_voltage); //计算休眠时间，均匀延时（30分-5分)
                else
                    sleepsec = 30; //延迟30秒重试

                last_send = millis(); //保存最后发送的时间
                client_aprs.stop();   //关闭已经创建的连接

                //没有到达延迟时间前一直等待
                while (millis() - last_send < sleepsec * 1000)
                {
                    //如果调试服务器是有数据发来，处理配置数据
                    if (client_dbg.available())
                        set_cfg();
                    voltageLOW();        //电压过低判断
                    ArduinoOTA.handle(); //OTA处理
                    delay(10);           //延时
                }
            }
        }
        //没能连接到调试服务器
        else
        {
            bool res = loginAPRS();       //发送数据包
            client_aprs.stop();           //关闭APRS服务器连接
            digitalWrite(LED_BUILTIN, 1); //关灯

            if (res)                                   //如果发送成功
                ESP.deepSleep(sleepsec * 1000 * 1000); //正常休眠
            else
                ESP.deepSleep(60 * 1000 * 1000); //休眠1分钟后重试
        }
    }
}
