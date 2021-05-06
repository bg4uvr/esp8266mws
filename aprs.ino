// ESP8266 APRS 气象站

#define DEBUG_MODE //调试模式时不把语句发往服务器

#ifdef DEBUG_MODE
#define SEND_INTERVAL 10 * 1000 //调试状态发送数据间隔（毫秒）
#else
#define SEND_INTERVAL 5 * 60 * 1000 //发送数据间隔（毫秒）
#endif
#define RECV_INTERVAL 30 * 1000 //接收心跳包的间隔， aprsc 2.1.5 服务器大约为20秒

#include <ESP8266WiFi.h>
#include <Wire.h>
#include <Time.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <Adafruit_BMP280.h>
#include <DHTesp.h>

WiFiClient client;                    //初始化WiFiclient实例
const char *host = "china.aprs2.net"; //APRS服务器地址
const int port = 14580;               //APRS服务器端口
bool auth = false;                    //APRS验证状态

uint32_t last_send;
uint32_t last_recv;
uint32_t data_cnt;

WiFiClient clientDBG;                  //初始化调试用WiFiclient实例
const char *hostDBG = "192.168.1.125"; //APRS服务器地址
const int portDBG = 14580;             //APRS服务器端口

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
        Serial.println(F("Failed to connect. Reset and try again..."));
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

//OTA更新
void Otasetup()
{
    // Port defaults to 8266
    // ArduinoOTA.setPort(8266);

    // Hostname defaults to esp8266-[ChipID]
    ArduinoOTA.setHostname("ARPS_OTA");

    // No authentication by default
    //ArduinoOTA.setPassword("admin");
    //ArduinoOTA.setPassword("21232f297a57a5a743894a0e4a801fc3");

    // Password can be set with it's md5 value as well
    // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
    //ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
        {
            type = "sketch";
        }
        else
        { // U_FS
            type = "filesystem";
        }

        // NOTE: if updating FS this would be the place to unmount FS using FS.end()
        Serial.println("Start updating " + type);
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR)
        {
            Serial.println("Auth Failed");
        }
        else if (error == OTA_BEGIN_ERROR)
        {
            Serial.println("Begin Failed");
        }
        else if (error == OTA_CONNECT_ERROR)
        {
            Serial.println("Connect Failed");
        }
        else if (error == OTA_RECEIVE_ERROR)
        {
            Serial.println("Receive Failed");
        }
        else if (error == OTA_END_ERROR)
        {
            Serial.println("End Failed");
        }
    });
    ArduinoOTA.begin();
    Serial.println("Ready");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
}

bool read_bmp280(float *temperature, float *pressure)
{
    Adafruit_BMP280 bmp; //初始化BMP280实例
    Serial.println("正在初始化BMP280传感器...");
    Wire.begin(2, 0); //重定义I2C端口
    if (!bmp.begin(BMP280_ADDRESS_ALT))
    {
        Serial.println(F("未找到BMP280传感器，请检查接线以及设置正确i2c地址(0x76 或 0x77)。"));
        return 0;
    }

    //设置BMP280采样参数
    bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                    Adafruit_BMP280::SAMPLING_X2,
                    Adafruit_BMP280::SAMPLING_X16,
                    Adafruit_BMP280::FILTER_X16,
                    Adafruit_BMP280::STANDBY_MS_500);

    Serial.println("BMP280传感器初始化成功");
    *temperature = bmp.readTemperature();
    *pressure = bmp.readPressure();
    return 1;
}

bool read_dht11(float *humidity)
{
    DHTesp dht;                  //DHT11实例
    dht.setup(5, DHTesp::DHT11); // Connect DHT sensor to GPIO 5
    *humidity = dht.getHumidity();
    if (*humidity > 100 || *humidity < 0)
        return 0;
    else
        return 1;
}

void setup()
{
    Serial.begin(115200); //配置串口
    WiFisetup();          //自动配网
    Otasetup();           //OTA更新
}

void msg(String msg)
{
    Serial.println(msg);
    if (clientDBG.connected())
        clientDBG.println(msg);
}

//发送数据
void send_data()
{
    char msgbuf[150] = {0}; //消息格式化缓存

    float humidity, temperatureBMP, pressure;   //保存湿度、温度、气压的浮点变量
    int humidityINT, temperatureF, pressureINT; //保存湿度、华氏温度、气压的整数变量

    bool dhtRES = read_dht11(&humidity);                   //读取DHT11湿度
    bool bmpRES = read_bmp280(&temperatureBMP, &pressure); //读取BMP280
    if (dhtRES)                                            //DHT11读取成功
    {
        snprintf(msgbuf, sizeof(msgbuf), "DHT11湿度：%0.2f", humidity);
        msg(msgbuf);
        humidityINT = humidity;
    }
    else //DHT11读取失败
    {
        msg("DHT11读取失败");
        humidityINT = 0; //湿度值设为0
    }

    if (bmpRES) //BMP280读取成功
    {
        snprintf(msgbuf, sizeof(msgbuf), "BMP280温度：%0.2f\tBMP280气压%0.2f", temperatureBMP, pressure);
        msg(msgbuf);
        temperatureF = temperatureBMP * 9 / 5 + 32; //转换成华氏度
        pressureINT = pressure / 10;                //把气压浮点数的Pa值转换成0.1hPa的整形数值
    }
    else //BMP280读取失败
    {
        msg("BMP280读取失败");
        temperatureF = 0; //温度值清零
        pressureINT = 0;  //气压值清零
    }

    //格式化发送语句
    //            -- c000s000g000t086r000p000h53b10020
    //            -- 每秒输出35个字节，包括数据末尾的换行符（OD,OA）
    //
    //            -- 数据解析：
    //            -- c000：风向角度，单位：度。
    //            -- s000：前1分钟风速，单位：英里每小时
    //            -- g000：前5分钟最高风速，单位：英里每小时
    //            -- t086：温度（华氏）
    //            -- r000：前一小时雨量（0.01英寸）
    //            -- p000：前24小时内的降雨量（0.01英寸）
    //            -- h53：湿度（00％= 100％）
    //            -- b10020：气压（0.1 hpa）
    snprintf(msgbuf, sizeof(msgbuf),
             "BG4UVR-10>APZESP,qAC,:=3153.47N/12106.86E_c000s000g000t%03dr000p000h%02db%05d send_cnt:%d, runtime:%ds",
             temperatureF, humidityINT, pressureINT, ++data_cnt, millis() / (1000));

#ifndef DEBUG_MODE
    client.println(msgbuf); //向服务器反馈信息
#endif

    last_send = millis(); //保存最后发送时间
    msg(msgbuf);          //语句同时发送的串口
}

void loop()
{
    //尝试连接本地调试监控服务器
    if (!clientDBG.connected())
        clientDBG.connect(hostDBG, portDBG);

    //如果尚未建立APRS服务器连接
    if (!client.connected())
    {
        msg("APRS服务器未连接，正在连接...");
        if (client.connect(host, port)) //连接APRS服务器成功
        {
            last_recv = millis();
            msg("APRS服务器已连接");
        }
        else
        {
            msg("APRS服务器连接失败，稍后重试");
            delay(5000);
        }
    }
    //接收超时主动断开连接
    else if (millis() - last_recv > RECV_INTERVAL)
    {
        auth = false;
        client.stop();
        msg("APRS服务器数据接收超时，已断开");
    }

    //如果缓冲区字符串大于0
    if (client.available())
    {
        last_recv = millis();                       //更新最后接收数据时间
        String line = client.readStringUntil('\n'); //获取字符串
        msg(line);                                  //把字符串传给串口

        if (auth == false)
        {
            if (line.indexOf("aprsc") != -1) //语句含有 aprsc
            {
                msg("正在登录ARPS服务器...");
                String loginmsg = "user BG4UVR-10 pass 21410 vers esp-01s 0.1 filter m/10"; //APRS登录命令
                client.println(loginmsg);                                                   //发送登录语句
                msg(loginmsg);
            }
            else if (line.indexOf("verified") != -1)
            {
                auth = true; //验证成功
                msg("APRS服务器登录验证已通过");
                send_data(); //发送登录后第一个数据包
            }
        }
    }

    //在已验证情况下，每间隔定时周期，发送一次数据
    if (auth == true && millis() - last_send >= SEND_INTERVAL)
        send_data();

    //扫描OTA任务
    ArduinoOTA.handle();
}
