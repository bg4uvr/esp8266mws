// ESP8266 APRS 气象站

//#define DEBUG_MODE //调试模式时不把语句发往服务器

#include <ESP8266WiFi.h>
#include <Wire.h>
#include <Time.h>
#include <WiFiManager.h>
#include <Adafruit_BMP280.h>
#include <DHTesp.h>
#include <EEPROM.h>
#include <ArduinoOTA.h>

#define EEP_SIZE 4
#define SYS_ADDR 0
#define SYS_SLEEP 0x55
#define SYS_RUN 0xaa

//系统状态
enum sys_mode_t
{
    SYSMODE_UNKNOWN,
    SYSMODE_RUN,
    SYSMODE_SLEEP,
};

//配置数据
struct cfg_t
{
    char aprs_server_addr[25];  //APRS服务器地址
    uint16_t aprs_server_port;  //APRS服务器端口
    char debug_server_addr[25]; //调试主机地址
    uint16_t debug_server_port; //调试主机端口
    char callsign[6];           //呼号
    char ssid[2];               //SSID
    uint16_t password;          //APRS密码
    uint16_t max_send_interval; //最大发送间隔
    uint16_t min_send_interval; //最小发送间隔
    float suspend_voltage;      //休眠电压
    sys_mode_t sysmode;         //系统状态
    uint16_t crc;               //校验值
};

cfg_t mycfg; //配置数据

const char *host = "china.aprs2.net"; //APRS服务器地址
const int port = 14580;               //APRS服务器端口

WiFiClient client_aprs, client_dbg; //实例化aprs服务器连接和调试连接
float voltage;                      //电池电压
uint16_t sleepsec;                  //下次工作延时
uint8_t runmode;                    //当前运行状态

//计算校验和

uint16_t calcCRC(char *p, int len)
{
    uint16_t t = 0;
    int i = 0;
    for (i = 0; i < len; i++)
    {
        t += p[i];
    }
    return t;
}

//校验和检测
bool checkCRC(unsigned char *p, int len)
{
    int i = 0;
    uint16_t t = 0;
    for (i = 0; i < len - 2; i++)
    {
        t += p[i];
    }
    if ((uint8_t)(t / 256) == p[len - 2] && (uint8_t)(t % 256) == p[len - 1])
    {
        return true;
    }
    else
        return false;
}

//自动配网
void WiFisetup()
{
    //WiFiManager
    //Local intialization. Once its business is done, there is no need to keep it around
    WiFiManager wifiManager;
    //wifiManager.resetSettings();
    wifiManager.setConfigPortalTimeout(60);
    //1 minute

    if (!wifiManager.autoConnect("APRS_SET"))
    {
        Serial.println("Failed to connect. Reset and try again...");

        delay(3000);
        ESP.reset();
        //重置并重试
        delay(5000);
    }

    //if you get here you have connected to the WiFi
    Serial.println("WiFi Connected!");
    Serial.print("IP ssid: ");
    Serial.println(WiFi.SSID());
    Serial.print("IP addr: ");
    Serial.println(WiFi.localIP());
}

bool read_bmp280(float *temperature, float *pressure)
{
    Adafruit_BMP280 bmp; //初始化BMP280实例
    Serial.println("正在初始化BMP280传感器...");
    Wire.begin(0, 5); //重定义I2C端口
    if (!bmp.begin(BMP280_ADDRESS_ALT))
    {
        return false;
    }
    Serial.println("BMP280传感器初始化成功");

    //设置BMP280采样参数
    bmp.setSampling(Adafruit_BMP280::MODE_FORCED, //FORCE模式读完自动转换回sleep模式？
                    Adafruit_BMP280::SAMPLING_X1,
                    Adafruit_BMP280::SAMPLING_X4,
                    Adafruit_BMP280::FILTER_X16,
                    Adafruit_BMP280::STANDBY_MS_1);
    Serial.println("正在读取BMP280传感器");
    *temperature = bmp.readTemperature();
    *pressure = bmp.readPressure();
    Serial.println("BMP280读取完成");
    return true;
}

bool read_dht11(float *humidity)
{
    DHTesp dht; //DHT11实例
    Serial.println("正在初始化DHT11传感器...");
    dht.setup(14, DHTesp::DHT11); // Connect DHT sensor to GPIO5
    Serial.println("正在读取DHT11传感器");
    *humidity = dht.getHumidity();

    if (dht.getStatus() == dht.ERROR_NONE)
    {
        Serial.println("DHT11读取成功");
        return true;
    }
    else
    {
        return false;
    }
}

//发送数据
void send_data()
{
    char msgbuf[150] = {0}; //消息格式化缓存

    float humidity, temperatureBMP, pressure;   //保存湿度、温度、气压的浮点变量
    int humidityINT, temperatureF, pressureINT; //保存湿度、华氏温度、气压的整数变量

    bool bmpRES = read_bmp280(&temperatureBMP, &pressure); //读取BMP280
    bool dhtRES = read_dht11(&humidity);                   //读取DHT11湿度

    if (dhtRES) //DHT11读取成功
    {
#ifdef DEBUG_MODE
        snprintf(msgbuf, sizeof(msgbuf), "DHT11湿度：%0.2f", humidity);
        Serial.println(msgbuf);
#endif
        humidityINT = humidity; //湿度转换为整数
        if (humidityINT > 100)  //限制范围1-99
            humidityINT = 99;
        if (humidity < 0)
            humidityINT = 1;
    }
    else //DHT11读取失败
    {
        Serial.println("DHT11读取失败");
        humidityINT = 50; //湿度值设为50
    }

    if (bmpRES) //BMP280读取成功
    {
#ifdef DEBUG_MODE
        snprintf(msgbuf, sizeof(msgbuf), "BMP280温度：%0.2f\tBMP280气压%0.2f", temperatureBMP, pressure);
        Serial.println(msgbuf);
#endif
        temperatureF = temperatureBMP * 9 / 5 + 32; //转换成华氏度
        pressureINT = pressure / 10;                //把气压浮点数的Pa值转换成0.1hPa的整形数值
    }
    else //BMP280读取失败
    {
        Serial.println("BMP280读取失败");
        temperatureF = 25;   //温度值设置为25度
        pressureINT = 10132; //气压值设置为10132
    }

    if (runmode == SYS_RUN)
        snprintf(msgbuf, sizeof(msgbuf),
                 "BG4UVR-10>APZESP,qAC,:=3153.47N/12106.86E_c000s000g000t%03dr000p000h%02db%05d battery:%0.3fV; next report: %dmins later.",
                 temperatureF, humidityINT, pressureINT, voltage, sleepsec / 60);
    else if (runmode == SYS_SLEEP)
        snprintf(msgbuf, sizeof(msgbuf),
                 "BG4UVR-10>APZESP,qAC,:=3153.47N/12106.86E_c000s000g000t%03dr000p000h%02db%05d battery too low, system suspended!",
                 temperatureF, humidityINT, pressureINT);
    else
        Serial.println("当前运行状态未知");

#ifndef DEBUG_MODE
    client_aprs.println(msgbuf); //数据发往服务器
#endif
    Serial.println(msgbuf); //数据发往串口
}

bool loginAPRS()
{
    uint8_t retrycnt = 0;
    Serial.println("正在连接APRS服务器");
    do
    {
        if (client_aprs.connect(host, port))
        {
            Serial.println("APRS服务器已连接");
            do
            {
                uint8_t recv_cnt = 0;
                if (client_aprs.available()) //如果缓冲区字符串大于0
                {
                    String line = client_aprs.readStringUntil('\n'); //获取字符串
                    Serial.print(line);                              //把字符串传给串口

                    if (line.indexOf("aprsc") != -1) //语句含有 aprsc
                    {
                        Serial.println("正在登录ARPS服务器...");
                        String loginmsg = "user BG4UVR-10 pass 21410 vers esp-01s 0.1 filter m/10"; //APRS登录命令
                        client_aprs.println(loginmsg);                                              //发送登录语句
                        Serial.println(loginmsg);
                    }
                    else if (line.indexOf("verified") != -1)
                    {
                        Serial.println("APRS服务器登录成功");
                        send_data(); //发送数据
                        Serial.println("本次数据发送已完成");
                        return true;
                    }
                    if (++recv_cnt > 5)
                    {
                        Serial.println("错误：没能从服务器接收到预期类型的数据");
                        return false;
                    };
                }
                else
                    delay(2000);
            } while (++retrycnt < 5);
            Serial.println("错误：接收APRS服务器数据超时");
            return false;
        }
        else
        {
            Serial.print('.');
            delay(1000);
        }
    } while (++retrycnt < 10);
    Serial.println("\n错误：连接APRS失败次数过多，已退出重试");
    return false;
}

void send_once()
{

    WiFisetup();              //自动配网连接WiFi
    if (loginAPRS() == false) //登录APRS服务器并发送数据
        sleepsec = 60;        //如果失败休眠1分钟后再试
    client_aprs.stop();       //关闭已经创建的连接
}

void setup()
{
    pinMode(2, OUTPUT);           //GPIO为LED
    digitalWrite(LED_BUILTIN, 0); //点亮

#ifdef DEBUG_MODE
    Serial.begin(115200); //配置串口
#endif

    voltage = 4.11f * analogRead(A0) / 1024; //读取并计算电池电压
    Serial.printf("当前电压：%0.3fV\r\n", voltage);

    //电压不小于3.0V才处理
    if (voltage >= 3.0f)
    {
        EEPROM.begin(EEP_SIZE);          //初始化EEPROM
        runmode = EEPROM.read(SYS_ADDR); //读取系统休眠状态

        if (voltage >= 3.3f) //在3.3V-4.2V区间
        {
            //如果当前不是运行状态，更改为运行状态
            if (runmode != SYS_RUN)
            {
                EEPROM.write(SYS_ADDR, SYS_RUN);
                EEPROM.commit();
                EEPROM.end(); //写入FLASH
                runmode = SYS_RUN;
            }

            sleepsec = 300 + (4.2f - voltage) * 1500 / (4.2f - 3.3f); //计算休眠时间，均匀延时（30分-5分)

            send_once();
        }

        else if (runmode != SYS_SLEEP) //如果电压低于3.4V，而且是不是休眠状态，转入休眠状态
        {
            EEPROM.write(SYS_ADDR, SYS_SLEEP);
            EEPROM.commit();
            EEPROM.end(); //写入FLASH
            runmode = SYS_SLEEP;

            sleepsec = 60 * 60; //并休眠60分钟
            Serial.println("警告：当前电压低于3.3V，停止数据处理");

            //发最后一次数据
            send_once();
        }

        sleepsec = sleepsec = 60 * 60; //休眠60分钟
    }
    //电压小于3.2V时不处理数据
    else
        sleepsec = 60 * 60; //直接休眠60分钟

    digitalWrite(LED_BUILTIN, 1); //关灯

#ifdef DEBUG_MODE
    ESP.deepSleep(10 * 1000 * 1000); //休眠10秒
#else
    ESP.deepSleep(sleepsec * 1000 * 1000); //休眠
#endif
}

void loop()
{
}
