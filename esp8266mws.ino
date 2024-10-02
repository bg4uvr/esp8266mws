/*
    Esp8266MWS     (Esp8266 Mini Weather Station)
                                bg4uvr @ 2021.5
*/
// #define DEBUG_MODE //调试模式时不把语句发往服务器 Do not send statements to the server while debugging mode
// #define EEPROM_CLEAR //调试时清除EEPROM Clear EEPROM while debugging

const char fw[20] = {"0.17a"}; // firmware version number

// 包含头文件
//  Include header file
#include <ESP8266WiFi.h>
#include <Wire.h>
#include <WiFiManager.h>
#include <Adafruit_BMP280.h>
#include <EEPROM.h>
#include <ArduinoOTA.h>
#include <Adafruit_AHTX0.h>
#include <unistd.h>
#include <time.h>

// ADC模式设置为测量电源模式
// ADC mode is set to measurement power mode
ADC_MODE(ADC_VCC);

// 判断到已连接调试主机时，将把调试消息发往主机
//  When the debug host is determined to be connected, the debug message is sent to the host
#define DBGPRINT(x)             \
    if (client_dbg.connected()) \
        client_dbg.print(x);

#define DBGPRINTLN(x)           \
    if (client_dbg.connected()) \
        client_dbg.println(x);

// 系统状态枚举
//  System state enumeration
typedef enum
{
    SYS_FAIL, // 非法状态    // Illegal status
    SYS_CFG,  // 配置状态    // Configure the state
    SYS_RUN,  // 运行状态    // Run status
} sys_mode_t;

// 语言枚举
//  Language enumeration
typedef enum
{
    CN = 0,
    EN = 1,
} language_t;

#define LANGUAGE_NUM 2

// 配置数据结构
//  Configure the data structure
typedef struct
{
    uint32_t crc;               // 校验值            // Check the value
    float lon;                  // 经度              // longitude
    float lat;                  // 纬度              // latitude
    uint16_t password;          // APRS密码          // APRS password
    uint16_t send_interval;     // 最小发送间隔      //Minimum transmission interval
    uint16_t aprs_server_port;  // APRS服务器端口    //APRS server port
    uint16_t debug_server_port; // 调试主机端口      //Debug host port
    char aprs_server_addr[26];  // APRS服务器地址    //APRS server address
    char debug_server_addr[26]; // 调试主机地址      //Debugging host address
    char callsign[8];           // 呼号              //callsign
    char ssid[4];               // SSID
    sys_mode_t sysstate;
    language_t language; // 语言              //language
} cfg_t;

// 系统全局变量
//  System global variable
cfg_t mycfg;                        // 系统配置参数                  //System configuration parameters
WiFiClient client_aprs, client_dbg; // 实例化aprs服务器连接和调试连接  //Instantiate APRS server connections and debug connections
Adafruit_BMP280 bmp;                // BMP280实例
Adafruit_AHTX0 aht;                 // AHT20实例
float voltage;                      // 电池电压                      //The battery voltage
uint32_t last_send;                 // 上一次发送时刻                //Last Send Time
uint32_t sleepsec;
char msgbuf[150] = {0};   // 消息格式化缓存                //Message format cache
bool bm280_state = false; // bmp280状态（初始化是否成功）
bool aht20_state = false; // aht20状态（初始化是否成功）

// CRC32 校验程序
// CRC32 Check
uint32_t mycrc32(uint8_t *data, int length)
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

// 自动配网
//  Automatic distribution network
void WiFisetup()
{
    WiFiManager wifiManager;
    // wifiManager.resetSettings();
    wifiManager.setConfigPortalTimeout(300);

    if (!wifiManager.autoConnect("Esp8266MWS-SET"))
    {
        delay(3000);
        ESP.reset();
        // 重置并重试
        //  Reset and retry
        delay(5000);
    }
}

// 读取BMP280
//  read BMP280
bool read_bmp280(float *temperature, float *pressure)
{
    if (bm280_state == false)
    {
        const char *msg1[] = {
            "BMP280读取失败",
            "BMP280 read failed",
        };
        DBGPRINTLN(msg1[mycfg.language]);
        return false;
    }

    // 设置BMP280采样参数
    //  Set the BMP280 sampling parameter
    bmp.setSampling(Adafruit_BMP280::MODE_FORCED, // FORCE模式读完自动转换回sleep模式 // return to sleep mode automatically after reading in FORCE mode
                    Adafruit_BMP280::SAMPLING_X1,
                    Adafruit_BMP280::SAMPLING_X4,
                    Adafruit_BMP280::FILTER_X16,
                    Adafruit_BMP280::STANDBY_MS_1);
    *temperature = bmp.readTemperature();
    *pressure = bmp.readPressure();
    const char *msg2[] = {
        "BMP280读取成功",
        "BMP280 read successfully",
    };
    DBGPRINTLN(msg2[mycfg.language]);
    return true;
}

// 读取AHT20
//  read AHT20
bool read_aht20(float *temperature, float *humidity)
{
    if (aht20_state == false)
    {
        const char *msg1[] = {
            "AHT20读取失败",
            "AHT20 read failed",
        };
        DBGPRINTLN(msg1[mycfg.language]);
        return false;
    }

    sensors_event_t humAHT, tempAHT;

    aht.getEvent(&humAHT, &tempAHT);

    *temperature = tempAHT.temperature;
    *humidity = humAHT.relative_humidity;
    const char *msg2[] = {
        "AHT20读取成功",
        "AHT20 read successfully",
    };
    DBGPRINTLN(msg2[mycfg.language]);
    return true;
}

// 读取传感器并发送一次数据
//  Read the sensor and send the data once
void send_data()
{
    float temperatureAHT, humidity, temperatureBMP, pressure; // 保存湿传感器的温度温度度，BMP280的温度、气压的浮点变量
    // Save the humidity sensor temperature, BMP280 temperature, air pressure floating point variables
    char temperatureS[4] = {"..."};
    char humidityS[3] = {".."};
    char pressureS[6] = {"....."};

    bool bmpRES = read_bmp280(&temperatureBMP, &pressure); // 读取BMP280         Read BMP280
    bool ahtRES = read_aht20(&temperatureAHT, &humidity);  // 读取AHT20温度湿度  Read AHT20 temperature and humidity

    // BMP280读取成功
    // BMP280 read successfully
    if (bmpRES)
    {
        snprintf(temperatureS, sizeof(temperatureS), "%03d", (int8_t)(temperatureBMP * 9 / 5 + 32)); // 保存温度字符串   // Save the temperature string
        snprintf(pressureS, sizeof(pressureS), "%05d", (uint16_t)(pressure / 10));                   // 保存气压字符串   // Save the barometric string
        if (client_dbg.connected())
        {
            switch (mycfg.language)
            {
            case CN:
                client_dbg.printf("\nbmp280温度:%0.2f\tbmp280气压:%0.2f\n", temperatureBMP, pressure);
                break;
            case EN:
                client_dbg.printf("\nbmp280 temperature:%0.2f\tbmp280 pressure:%0.2f\n", temperatureBMP, pressure);
                break;
            default:
                break;
            }
        }
    }
    // AHT20读取成功
    // AHT20 read successfully
    if (ahtRES)
    {
        snprintf(temperatureS, sizeof(temperatureS), "%03d", (int8_t)(temperatureAHT * 9 / 5 + 32)); // 保存温度字符串   // Save the temperature string
        snprintf(humidityS, sizeof(humidityS), "%02d", (uint8_t)humidity);                           // 保存湿度字符串   // Save the humidity string
        if (client_dbg.connected())
        {
            switch (mycfg.language)
            {
            case CN:
                client_dbg.printf("aht20温度:%0.2f\taht20湿度:%0.2f\n", temperatureAHT, humidity);
                break;
            case EN:
                client_dbg.printf("aht20 temperature:%0.2f\taht20 humidity:%0.2f\n", temperatureAHT, humidity);
                break;
            default:
                break;
            }
        }
    }
    // 如果BMP280和AHT20均读取成功，那么平均两个传感器的温度
    //  If both BMP280 and AHT20 are read successfully, then average the temperature of both sensors
    if (ahtRES && bmpRES)
    {
        snprintf(temperatureS, sizeof(temperatureS), "%03d", (int8_t)(((temperatureAHT + temperatureBMP) / 2) * 9 / 5 + 32));
        if (client_dbg.connected())
            switch (mycfg.language)
            {
            case CN:
                client_dbg.printf("两传感器平均温度:%0.2f\n", (temperatureAHT + temperatureBMP) / 2);
                break;
            case EN:
                client_dbg.printf("Average temperature of two sensors:%0.2f\n", (temperatureAHT + temperatureBMP) / 2);
                break;
            default:
                break;
            }
    }

    // 发送用户自定义消息 send user custom messsage
    uint8_t sync_timeout = 50;                                                 // NTP同步超时计数器，设置为5秒
    configTime(0, 0, "ntp.aliyun.com", "time.asia.apple.com", "pool.ntp.org"); // 设置时间格式以及时间服务器的网址
    DBGPRINTLN("Waiting for time sync from NTP server");                       // 等待时间和NTP服务器同步
    while (time(nullptr) <= 100000 && sync_timeout--)                          // 这里需要多读几次，以等待时间同步完成（较早的时间可以认为同步尚未完成）
    {
        DBGPRINT(".");
        delay(100);
    }
    time_t now = time(nullptr);                                                            // 获取当前时间
    struct tm *timenow = gmtime(&now);                                                     // 转换成年月日的数字
    snprintf(msgbuf, sizeof(msgbuf), "\nGMT time is: %s", asctime(timenow));               // 格式化时间
    DBGPRINTLN(msgbuf);                                                                    // 发送到调试主机显示
    if ((timenow->tm_hour % 3 == 0) && (timenow->tm_min < (mycfg.send_interval / 60) + 1)) // 指定时间间隔发送一次（最多可能会多发一次）
    {
        snprintf(msgbuf, sizeof(msgbuf), "%s-%s>APUVR:>esp8266mws ver%s https://github.com/bg4uvr/esp8266mws", mycfg.callsign, mycfg.ssid, fw);
#ifndef DEBUG_MODE
        client_aprs.println(msgbuf); // 数据发往服务器   // The data is sent to the server
#endif
        DBGPRINTLN(msgbuf);
    }

    // 发送气象报文 Send weather messages
    snprintf(msgbuf, sizeof(msgbuf),
             "%s-%s>APUVR:!%07.2f%cR%08.2f%c_.../...g...t%sr...p...h%sb%sBat:%d",
             mycfg.callsign, mycfg.ssid, mycfg.lat, mycfg.lat > 0 ? 'N' : 'S', mycfg.lon, mycfg.lon > 0 ? 'E' : 'W',
             temperatureS, humidityS, pressureS, ESP.getVcc());

#ifndef DEBUG_MODE
    client_aprs.println(msgbuf); // 数据发往服务器   // The data is sent to the server
#endif
    DBGPRINTLN(msgbuf);
}

// 登陆APRS服务器发送数据
//  Log on to the APRS server and send data
bool loginAPRS()
{
    const char *msg[] = {
        "正在连接APRS服务器",
        "Connecting to the APRS server",
    };
    DBGPRINTLN(msg[mycfg.language]);

    uint8_t timeout = 0;  // 超时计数器
    while (timeout++ < 5) // 5次都未能成功连接服务器 // Failed to connect to the server
    {
        if (client_aprs.connect(mycfg.aprs_server_addr, mycfg.aprs_server_port))
        {
            const char *msg1[] = {
                "APRS服务器已连接",
                "The APRS server is connected",
            };
            DBGPRINTLN(msg1[mycfg.language]);

            timeout = 0;            // 超时计数清零
            while (timeout++ < 100) // 等待发送成功
            {
                if (client_aprs.available()) // 如果缓冲区字符串大于0    // If the buffer string is greater than 0
                {
                    String line = client_aprs.readStringUntil('\n'); // 获取字符串       // Get the string
                    DBGPRINTLN(line);                                // 转发到调试主机   // Forward to the debug host

                    // 如果已经连接到服务器，则开始登录
                    //  If you are already connected to the server, start logging in
                    if (line.indexOf("aprsc") != -1 ||     // aprsc服务器
                        line.indexOf("javAPRSSrvr") != -1) // javAPRSSrvr服务器
                    {
                        const char *msg2[] = {
                            "正在登录ARPS服务器...",
                            "Logging on to the ARPS server...",
                        };
                        DBGPRINTLN(msg2[mycfg.language]);
                        sprintf(msgbuf, "user %s-%s pass %d vers esp8266mws %s", mycfg.callsign, mycfg.ssid, mycfg.password, fw);
                        client_aprs.println(msgbuf); // 发送登录语句 // Send the logon statement
                        DBGPRINTLN(msgbuf);
                        timeout = 0; // 超时计数清零
                    }
                    // 登陆验证成功或者失败都发送数据（失败“unverified”也包含“verified”，验证失败也可发送数据，但会显示未验证）
                    //  Send data if login verification is successful or fails (" Unverified "includes" Verified "if failed, or data can be sent if verification fails, but it will show" Unverified ")
                    else if (line.indexOf("verified") != -1)
                    {
                        const char *msg3[] = {
                            "APRS服务器登录成功",
                            "APRS server login successful",
                        };
                        DBGPRINTLN(msg3[mycfg.language]);
                        send_data();                  // 发送数据 send data
                        if (client_aprs.flush(10000)) // 最长等待10秒来完成发送 Wait up to 10 seconds to complete the transmission
                        {
                            const char *msg100[] = {
                                "数据发送成功",
                                "data sent success",
                            };
                            DBGPRINTLN(msg100[mycfg.language]);
                            return true;
                        }
                        else
                        {
                            const char *msg101[] = {
                                "数据发送超时",
                                "data sending timeout",
                            };
                            DBGPRINTLN(msg101[mycfg.language]);
                            return false;
                        }
                    }
                    // 服务器已满 Server full
                    else if (line.indexOf("Server full") != -1 ||
                             line.indexOf("Port full") != -1)
                    {
                        const char *msg4[] = {
                            "服务器已负荷已满，将稍后重试",
                            "The server is full and will try again later",
                        };
                        DBGPRINTLN(msg4[mycfg.language]);
                        return false;
                    }
                }
                delay(100); // 每0.1秒检查一次是否接收到新数据
            }
            // 10秒未接收预期数据，认为超时
            const char *msg6[] = {
                "错误：接收APRS服务器数据超时",
                "Error: Receive APRS server data timeout",
            };
            DBGPRINTLN(msg6[mycfg.language]);
            return false;
        }
        // 连接APRS服务器失败
        //  Failed to connect to the APRS server
        else
            delay(2000);
    }

    const char *msg7[] = {
        "\n5次未能成功连接APRS服务器，将休眠1分钟后再重试",
        "\nError: Failing to connect to APRS server for 5 times, will sleep for 1 minute and then try again",
    };
    DBGPRINTLN(msg7[mycfg.language]);
    return false;
}

// 显示配置数据
//  Display the configuration data
void dispset()
{
    switch (mycfg.language)
    {
    case CN:
        client_dbg.println("系统当前配置：");
        client_dbg.print("aprs服务器地址:\t");
        client_dbg.println(mycfg.aprs_server_addr);
        client_dbg.print("aprs服务器端口:\t");
        client_dbg.println(mycfg.aprs_server_port);
        client_dbg.print("调试主机地址:\t");
        client_dbg.println(mycfg.debug_server_addr);
        client_dbg.print("调试主机端口:\t");
        client_dbg.println(mycfg.debug_server_port);
        client_dbg.print("呼号:\t");
        client_dbg.println(mycfg.callsign);
        client_dbg.print("SSID:\t");
        client_dbg.println(mycfg.ssid);
        client_dbg.print("APRS验证码:\t");
        client_dbg.println(mycfg.password);
        client_dbg.print("发送间隔:\t");
        client_dbg.println(mycfg.send_interval);
        client_dbg.print("经度:\t");
        client_dbg.printf("%0.2f\n", mycfg.lon);
        client_dbg.print("纬度:\t");
        client_dbg.printf("%0.2f\n", mycfg.lat);
        client_dbg.print("语言设置:\t");
        client_dbg.println(mycfg.language);
        client_dbg.print("系统运行状态:\t");
        client_dbg.println(mycfg.sysstate);
        break;
    case EN:
        client_dbg.println("System current configuration:");
        client_dbg.print("APRS server address:\t");
        client_dbg.println(mycfg.aprs_server_addr);
        client_dbg.print("APRS server port:\t");
        client_dbg.println(mycfg.aprs_server_port);
        client_dbg.print("Debug host address:\t");
        client_dbg.println(mycfg.debug_server_addr);
        client_dbg.print("Debug host port:\t");
        client_dbg.println(mycfg.debug_server_port);
        client_dbg.print("Callsign:\t");
        client_dbg.println(mycfg.callsign);
        client_dbg.print("SSID:\t");
        client_dbg.println(mycfg.ssid);
        client_dbg.print("APRS verification code:\t");
        client_dbg.println(mycfg.password);
        client_dbg.print("Ssend interval:\t");
        client_dbg.println(mycfg.send_interval);
        client_dbg.print("Longitude:\t");
        client_dbg.printf("%0.2f\n", mycfg.lon);
        client_dbg.print("Latitude:\t");
        client_dbg.printf("%0.2f\n", mycfg.lat);
        client_dbg.print("Language:\t");
        client_dbg.println(mycfg.language);
        client_dbg.print("System state:\t");
        client_dbg.println(mycfg.sysstate);
        break;
    default:
        break;
    }
}

// 显示系统信息
//  Display system information
void dispsysinfo()
{
    // 显示系统名称
    //  Display the system name
    client_dbg.println("\nEsp8266MWS 迷你气象站");
    client_dbg.println("\nEsp8266MWS Mini Weather Station");

    // 显示设备当前局域网IP地址
    //  Displays the device's current LAN IP address
    const char *msg[] = {
        "\n当前设备IP地址为：",
        "\nThe current device IP address is:",
    };
    DBGPRINTLN(msg[mycfg.language]);
    client_dbg.println(WiFi.localIP());

    // 显示当前配置
    //  Displays the current configuration
    dispset();

    // 显示提示消息
    //  Display the prompt message
    switch (mycfg.language)
    {
    case CN:
        client_dbg.println("\n\
配置命令格式说明：\n\
\n\
    cfg -参数1 参数1数据 [-参数2 参数2数据] [...] [-参数n 参数n数据]\n\
\n\
    参数    含义            格式                说明\n\
\n\
必设参数:\n\
    -c      呼号            BGnXXX              个人台站的呼号\n\
    -w      验证码          12345               这个验证码的来源不解释\n\
\n\
可选参数：\n\
    -o      经度            12106.00            格式：dddmm.mm，东正西负\n\
    -a      纬度            3153.00             格式：ddmm.mm，北正南负\n\
    -s      APRS服务器地址  xxx.aprs2.net       不解释\n\
    -d      SSID            13                  SSID(支持2位字母的新规则)\n\
    -p      APRS服务器端口  14580               不解释\n\
    -g      调试主机地址    192.168.1.125       用于调试、配置及监控的主机内网IP\n\
    -e      调试主机端口    12345               不解释\n\
    -n      发送间隔        900                 单位：秒（范围600-1200）\n\
    -l      语言选择        CN                  0 中文；1 英文\n\
\n\
配置命令示例:\n\
    cfg -c BGnXXX -w 12345\n\
    cfg -c BGnXXX -w 12345 -o 12100.00 -a 3100.00 -s xxx.aprs2.net\n\
\n\
更改语言 (Change language)：\n\
    cfg -l n\n\
    n = 0 中文\n\
      = 1 English\n\
      = 2 xxx (等待你来完成 Waiting for you to finish~)\n\
\n\
复位重启命令：\n\
    rst\n\
\n\
    所有命令发送时需要以 \\n 来结尾：\n\
        rst\\n\n\
");
        break;
    case EN:
        client_dbg.println("\n\
Format description of configuration command:\n\
\n\
    cfg -parameter1 parameter1_data [-parameter2 parameter2_data] [...] [-parameterN parameterN_data]\n\
\n\
 parameter  means               sample              instructions\n\
\n\
Required parameters:\n\
    -c      callsign            BGnXXX              Call sign of amateur radio station\n\
    -w      Verification code   12345               The origin of this CAPTCHA is not explained\n\
\n\
Optional parameters:\n\
    -a      latitude            3153.00             Format：ddmm.mm，N plus and S minus\n\
    -o      longitude           12106.00            Format：dddmm.mm，E plus and W minus\n\
    -s      APRS server address xxx.aprs2.net       Don't explain\n\
    -d      SSID                13                  SSID(New rule to support 2-bit letters)\n\
    -p      APRS server port    14580               don't explain\n\
    -g      Debug host address  192.168.1.125       host Intranet IP for debugg,config,monitor\n\
    -e      Debug host port     12345               don't explain\n\
    -n      send interval   900                 Unit: Seconds (range: 600-1200)\n\
    -l      Language selection  CN                  0 Chinese, 1 English\n\
\n\
Examples of configuration commands:\n\
    cfg -c BGnXXX -w 12345\n\
    cfg -c BGnXXX -w 12345 -o 12100.00 -a 3100.00 -s xxx.aprs2.net\n\
\n\
Change language：\n\
    cfg -l n\n\
    n = 0 Chinese\n\
      = 1 English\n\
      = 2 xxx (Waiting for you to finish~)\n\
\n\
Rst command：\n\
    rst\n\
\n\
    All commands need to end with \\n when sending, such as:\n\
        rst\\n\n\
");
        break;
    default:
        break;
    }
}

// 保存配置数据
//  Save the configuration data
void eeprom_save()
{
    mycfg.crc = mycrc32((uint8_t *)&mycfg + 4, sizeof(mycfg) - 4); // 计算校验值       Calculate the checksum
    for (uint8_t i = 0; i < sizeof(cfg_t); i++)                    // 写入配置数据     Write configuration data
        EEPROM.write(i, ((uint8_t *)&mycfg)[i]);
    EEPROM.commit(); // 提交数据
}

// 配置数据初始化
//  Configure data initialization
void cfg_init()
{
    mycfg.lon = 100.0f; // default lon: 01d00.00`
    mycfg.lat = 100.0f; // default lat: 001d00.00`
    mycfg.send_interval = 900;
    mycfg.password = 0;
    mycfg.aprs_server_port = 14580;
    mycfg.debug_server_port = 12345;
    strcpy(mycfg.aprs_server_addr, "rotate.aprs2.net"); // default server
    strcpy(mycfg.debug_server_addr, "192.168.1.125");
    strcpy(mycfg.callsign, "NOCALL");
    strcpy(mycfg.ssid, "13");
    mycfg.sysstate = SYS_CFG;
    mycfg.language = CN;
    eeprom_save();
}

// 系统配置程序
//  System configurator
void set_cfg()
{
    // 如果没有接收到数据直接返回
    //  If no data is received, return it directly
    if (!client_dbg.available())
        return;

    // 获取调试服务器发来的字符串
    //  Get the string sent by the debug server
    String line = client_dbg.readStringUntil('\n'); // 每次解析到换行符  Each parse to a newline character
    const char *msg[] = {
        "\n您发送的命令为：",
        "\nThe command you sent is:",
    };
    DBGPRINTLN(msg[mycfg.language]);
    client_dbg.println(line); // 命令回显    // command echo

    // 开始解析命令字符串
    //  Begin parsing the command string
    char *buf = new char[line.length() + 1]; // 新建临时缓存，用于String类型转换为char[]类型     // Create a temporary cache for converting String to char[]

    strcpy(buf, line.c_str()); // 复制字符串     // Copy the string

    // is RST command?
    if (strncmp(buf, "rst", 3) == 0)
    {
        const char *msg0[] = {
            "收到复位使命令，重新启动中...\n",
            "Reset command received, restarting...\n",
        };
        DBGPRINTLN(msg0[mycfg.language]);
        delay(2000);
        ESP.reset(); // reset~
    }

    // 判断命令是否正确
    //  Determine if the command is correct
    if (strncmp(buf, "cfg ", 4) != 0)
    {
        const char *msg1[] = {
            "命令格式不正确，请重新输入",
            "Command format incorrect, please retry",
        };
        DBGPRINTLN(msg1[mycfg.language]);
        return;
    }

    // 分割存储参数
    //  Split storage parameters
    char *p;                                             // 新建用于分割字符串的指针 Create a new pointer to split the string
    char *cmd[50] = {0};                                 // 命令数组 Ordered array
    uint8_t cnt;                                         // 参数计数 Parameters of the count
    p = strtok(buf, " ");                                // 字符串中搜索空格 Search for Spaces in a string
    for (cnt = 0; cnt < sizeof(cmd) && p != NULL; cnt++) // 搜索配置参数，保存到指针数组 Search for configuration parameters and save them to a pointer array
    {
        cmd[cnt] = p;
        p = strtok(NULL, " ");
    }

    // 解析各参数
    //  Parse the parameters
    optind = 0; // optind 为getopt函数使用的全局变量，用于存储getopt的索引个数。此处必须清零，否则下次将无法工常工作
    /* Optind is a global variable used by getopt.    It is used to store the number of indexes of
      getopt.This place must be cleared, otherwise will not be able to work regularly next time */
    int ch;
    bool fail = false;
    bool ok = false;
    while ((ch = getopt(cnt, cmd, "c:w:o:a:s:d:p:e:g:v:r:n:x:l:")) != -1)
    {
        switch (ch)
        {
        case 'c':
            if (strlen(optarg) < 4 || strlen(optarg) > 6)
            {
                const char *msg3[] = {
                    "呼号长度小于4位或大于6位",
                    "Call sign length is less than 4 digits or longer than 6 digits",
                };
                DBGPRINTLN(msg3[mycfg.language]);
                fail = true;
            }
            else
            {
                strcpy(mycfg.callsign, optarg);
                ok = true;
            }
            break;
        case 'w':
            if (atol(optarg) < 0 || atol(optarg) > 32768)
            {
                const char *msg16[] = {
                    "密码不合法",
                    "Password is not valid",
                };
                DBGPRINTLN(msg16[mycfg.language]);
                fail = true;
            }
            else
            {
                mycfg.password = atoi(optarg);
                ok = true;
            }
            break;
        case 'o':
            if (atof(optarg) > 18000.0f || atof(optarg) < -18000.0f)
            {
                const char *msg11[] = {
                    "经度值超出范围",
                    "Longitude value out of range",
                };
                DBGPRINTLN(msg11[mycfg.language]);
                fail = true;
            }
            else
            {
                mycfg.lon = atof(optarg);
                ok = true;
            }
            break;
        case 'a':
            if (atof(optarg) > 9000.0f || atof(optarg) < -9000.0f)
            {
                const char *msg12[] = {
                    "纬度值超过范围",
                    "Latitude value out of range",
                };
                DBGPRINTLN(msg12[mycfg.language]);
                fail = true;
            }
            else
            {
                mycfg.lat = atof(optarg);
                ok = true;
            }
            break;
        case 's':
            if (strlen(optarg) > 25)
            {
                const char *msg17[] = {
                    "APRS服务器地址太长（最多25字节）",
                    "The APRS server address is too long(max 25 bytes)",
                };
                DBGPRINTLN(msg17[mycfg.language]);
                fail = true;
            }
            else
            {
                strcpy(mycfg.aprs_server_addr, optarg);
                ok = true;
            }
            break;
        case 'd':
            if (strlen(optarg) > 2 || strlen(optarg) == 0)
            {
                const char *msg4[] = {
                    "SSID 长度大于2位或是等于0",
                    "SSID length longer than 2 digits or equal to 0",
                };
                DBGPRINTLN(msg4[mycfg.language]);
                fail = true;
            }
            else
            {
                strcpy(mycfg.ssid, optarg);
                ok = true;
            }
            break;
        case 'p':
            if (atoi(optarg) < 20 || atoi(optarg) > 20000)
            {
                const char *msg20[] = {
                    "APRS服务器端口号不合法（正确范围：20-20000）",
                    "The APRS server port number is not valid (correct range: 20-20000)",
                };
                DBGPRINTLN(msg20[mycfg.language]);
                fail = true;
            }
            {
                mycfg.aprs_server_port = atoi(optarg);
                ok = true;
            }
            break;
        case 'g':
            if (strlen(optarg) > 25)
            {
                const char *msg18[] = {
                    "调试主机地址太长（最长25字节）",
                    "The debug host address is too long(max 25 bytes)",
                };
                DBGPRINTLN(msg18[mycfg.language]);
                fail = true;
            }
            else
            {
                strcpy(mycfg.debug_server_addr, optarg);
                ok = true;
            }
            break;
        case 'e':
            if (atoi(optarg) < 1024 || atoi(optarg) > 65000)
            {
                const char *msg21[] = {
                    "调试主机端口号不合法（正确范围：1024-65000）",
                    "The debug host port number is not valid (correct range: 1024-65000)",
                };
                DBGPRINTLN(msg21[mycfg.language]);
                fail = true;
            }
            else
            {
                mycfg.debug_server_port = atoi(optarg);
                ok = true;
            }
            break;
        case 'n':
            if (atoi(optarg) < 600 || atoi(optarg) > 1200)
            {
                const char *msg5[] = {
                    "发送间隔设置超出范围",
                    "The send interval setting out range",
                };
                DBGPRINTLN(msg5[mycfg.language]);
                fail = true;
            }
            else
            {
                mycfg.send_interval = atoi(optarg);
                ok = true;
            }
            break;
        case 'l':
            if (atoi(optarg) < 0 || atoi(optarg) >= LANGUAGE_NUM)
            {
                const char *msg13[] = {
                    "语言设置值超出有效范围",
                    "The language setting value is out of the valid range",
                };
                DBGPRINTLN(msg13[mycfg.language]);
                fail = true;
            }
            else
            {
                mycfg.language = (language_t)(atoi(optarg));
                ok = true;
            }
            break;

        default:
            break;
        }
    }

    delete[] buf; // 注销临时缓存 Unregister temporary cache

    // 如果有参数设置错误，不保存退出
    // If there is a parameter setting error, do not save and return
    if (fail == true || ok == false)
    {
        const char *msg15[] = {
            "设置的参数有误，设置未保存，请重新输入",
            "There is error in setting parameters. Settings are not saved. Please try again",
        };
        DBGPRINTLN(msg15[mycfg.language]);
        return;
    }

    // 判断是否已经设置必设参数
    //  Determines whether the required parameters have been set
    if (mycfg.callsign == "" || mycfg.password == 0)
    {
        const char *msg2[] = {
            "有必设参数未被设置，请重新输入。（呼号、密码这2项数据为必设参数）",
            "Required parameters are not set. Please reenter.(Callsign and Password must be set)",
        };
        DBGPRINTLN(msg2[mycfg.language]);
        return;
    }

    // 设置成功，保存退出
    //// Set successfully, save exit
    mycfg.sysstate = SYS_RUN; // 更改系统状态为运行状态      Change the system state to running state
    eeprom_save();            // 保存配置数据                Save configuration data
    dispset();                // 显示系统当前配置            Displays the current system configuration
    const char *msg14[] = {
        "设置已保存（注意：此处无法准确检查所有参数的正确性，请自行检查确认）",
        "Settings saved (Attention: that the correctness of all parameters cannot be checked accurately here. Please check and confirm by yourself.",
    };
    DBGPRINTLN(msg14[mycfg.language]);
    return;
}

// 连接调试主机时的空闲扫描
//  Idle scan when connecting the debug host
void freeloop()
{
    set_cfg();           // 处理配置命令 Processing configuration commands
    ArduinoOTA.handle(); // OTA处理      OTA processing
    delay(10);           // 延时         Time delay
}

// 系统初始化
//  System initialization
void setup()
{
    // 配置指示灯并点亮
    //  config and turnon LED
    pinMode(2, OUTPUT);
    digitalWrite(LED_BUILTIN, 0);

    WiFisetup();                              // 自动配网        Automatic distribution network
    ArduinoOTA.setHostname("Esp8266MWS-OTA"); // 设置OTA主机名   Set OTA hostname
    ArduinoOTA.begin();                       // 初始化OTA       Initialize OTA
    EEPROM.begin(256);                        // 初始化EEPROM    initialize EEPROM

#ifdef DEBUG_MODE
#ifdef EEPROM_CLEAR
    // 调试时用于清除EEPROM设置数据
    //  Use to clear EEPROM setup data during debugging
    for (uint8_t i = 0; i < 128; i++)
        EEPROM.write(i, 0xff);
    EEPROM.commit();
#endif
#endif

    // 读取系统配置数据
    //  Read the system configuration data
    for (uint8_t i = 0; i < sizeof(cfg_t); i++)
        *((uint8_t *)&mycfg + i) = EEPROM.read(i);

    // 校验配置数据，如果校验失败，初始化默认配置数据并进入置模式
    //  Validate the configuration data. If the validation fails, initialize the default configuration data and enter set mode
    if (mycrc32((uint8_t *)&mycfg + 4, sizeof(mycfg) - 4) != mycfg.crc)
        cfg_init();

    // 重定义I2C端口（SDA、SCL）     //Redefine I2C ports (SDA, SCL)
    Wire.begin(12, 14);

    // 初始化bmp280
    if (bmp.begin()) // if (!bmp.begin(BMP280_ADDRESS_ALT))
        bm280_state = true;

    // 初始化aht20
    if (aht.begin())
        aht20_state = true;
}

// 程序主循环
//  main loop
void loop()
{
    sleepsec = mycfg.send_interval;

    // 判断当前系统状态
    //  Determine the current system state
    switch (mycfg.sysstate)
    {
    // 运行状态
    //  run state
    case SYS_RUN:

        client_dbg.connect(mycfg.debug_server_addr, mycfg.debug_server_port);
        dispsysinfo();

        // 只要服务器连接未断开就一直循环运行
        //  Loops as long as the server connection is not broken
        do
        {
            // 如果登录发送数据 失败
            //  If logon and send data fail
            if (!loginAPRS())
                sleepsec = 60;

            client_aprs.stop();   // 关闭已经创建的连接  Close the connection that has been created
            last_send = millis(); // 保存最后发送的时间  Save the last sent time

            // 没有到达延迟时间，并且调试连接还在连接，一直等待
            //  There is no latency and the debug connection is still connected
            while (millis() - last_send < sleepsec * 1000 && client_dbg.connected())
                freeloop();
        } while (client_dbg.connected());

        client_aprs.stop(); // 关闭已经创建的连接   Close the connection that has been created
        delay(100);         // 延时0.1秒，以等待连接关闭完成 Delay 0.1 second to wait for the connection closing to complete

        // 工作完成，准备休眠
        //  Work done, ready for hibernation
        digitalWrite(LED_BUILTIN, 1);                    // 关灯 turnoff LED
        ESP.deepSleep((uint64_t)sleepsec * 1000 * 1000); // 休眠 sleep
        break;

    case SYS_CFG:
    default:
        // 一直尝试连接配置服务器
        //  Always try to connect to the configuration server
        while (!client_dbg.connect(mycfg.debug_server_addr, mycfg.debug_server_port))
        {
            ArduinoOTA.handle(); // OTA处理      OTA processing
            delay(2000);         // 延时2秒      Delay 2 seconds,
        }

        // 已连接到默认配置服务器，显示系统信息
        //  The default configuration server is connected
        dispsysinfo();

        client_dbg.println("系统配置数据校验失败，已重新初始化，请发送配置命令配置系统");
        client_dbg.println("System configuration data validation failed, has been reinitialized, please send configuration command to configure the system");

        // 此处解析配置命令，直到配置成功转为运行状态
        //  The configuration command is parsed here until the configuration is successfully turned into a running state
        while (client_dbg.connected() && mycfg.sysstate != SYS_RUN)
            freeloop();
        break;
    }
}
