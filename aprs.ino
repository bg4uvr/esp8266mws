// ESP8266 APRS 气象站

//#define DEBUG_MODE //调试模式时不把语句发往服务器

#include <ESP8266WiFi.h>
#include <Wire.h>
#include <Time.h>
#include <WiFiManager.h>
#include <Adafruit_BMP280.h>
#include <DHTesp.h>

const char *host = "china.aprs2.net"; //APRS服务器地址
const int port = 14580;               //APRS服务器端口

WiFiClient client; //初始化WiFiclient实例
float voltage;     //电池电压
uint16_t sleepsec; //下次工作延时

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

bool read_bmp280(float *temperature, float *pressure)
{
    Adafruit_BMP280 bmp; //初始化BMP280实例
    Serial.println("正在初始化BMP280传感器...");
    Wire.begin(0, 5); //重定义I2C端口
    if (!bmp.begin(BMP280_ADDRESS_ALT))
    {
        Serial.println(F("未找到BMP280传感器，请检查接线以及设置正确i2c地址(0x76 或 0x77)。"));
        return false;
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
    //设置BMP280采样参数
    bmp.setSampling(Adafruit_BMP280::MODE_SLEEP,
                    Adafruit_BMP280::SAMPLING_X2,
                    Adafruit_BMP280::SAMPLING_X16,
                    Adafruit_BMP280::FILTER_X16,
                    Adafruit_BMP280::STANDBY_MS_500);    

    return true;
}

bool read_dht11(float *humidity)
{
    DHTesp dht;                   //DHT11实例
    dht.setup(14, DHTesp::DHT11); // Connect DHT sensor to GPIO5

    float temp = dht.getHumidity();

    if (dht.getStatus() == dht.ERROR_NONE)
    {
        for (uint8_t i = 0; i < 2; i++)
        {
            delay(2000);
            temp += dht.getHumidity();
        }
        *humidity = temp / 3;
        return true;
    }
    else
        return false;
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
        snprintf(msgbuf, sizeof(msgbuf), "DHT11湿度：%0.2f", humidity);
        Serial.println(msgbuf);
        humidityINT = humidity; //湿度转换为整数
    }
    else //DHT11读取失败
    {
        Serial.println("DHT11读取失败");
        humidityINT = 0; //湿度值设为0
    }

    if (bmpRES) //BMP280读取成功
    {
        snprintf(msgbuf, sizeof(msgbuf), "BMP280温度：%0.2f\tBMP280气压%0.2f", temperatureBMP, pressure);
        Serial.println(msgbuf);
        temperatureF = temperatureBMP * 9 / 5 + 32; //转换成华氏度
        pressureINT = pressure / 10;                //把气压浮点数的Pa值转换成0.1hPa的整形数值
    }
    else //BMP280读取失败
    {
        Serial.println("BMP280读取失败");
        temperatureF = 0; //温度值清零
        pressureINT = 0;  //气压值清零
    }

    snprintf(msgbuf, sizeof(msgbuf),
             "BG4UVR-10>APZESP,qAC,:=3153.47N/12106.86E_c000s000g000t%03dr000p000h%02db%05d Battery:%0.2fV; Next report: after %d seconds.",
             temperatureF, humidityINT, pressureINT, voltage, sleepsec);

#ifndef DEBUG_MODE
    client.println(msgbuf); //向服务器反馈信息
#endif

    Serial.println(msgbuf); //语句同时发送的串口
}

bool loginAPRS()
{
    uint8_t retrycnt = 0;

    Serial.println("正在连接APRS服务器");
    do
    {
        if (client.connect(host, port))
        {
            Serial.println("APRS服务器已连接");
            do
            {
                uint8_t recv_cnt = 0;
                if (client.available()) //如果缓冲区字符串大于0
                {
                    String line = client.readStringUntil('\n'); //获取字符串
                    Serial.print(line);                         //把字符串传给串口

                    if (line.indexOf("aprsc") != -1) //语句含有 aprsc
                    {
                        Serial.println("正在登录ARPS服务器...");
                        String loginmsg = "user BG4UVR-10 pass 21410 vers esp-01s 0.1 filter m/10"; //APRS登录命令
                        client.println(loginmsg);                                                   //发送登录语句
                        Serial.println(loginmsg);
                    }
                    else if (line.indexOf("verified") != -1)
                    {
                        Serial.println("APRS服务器登录成功");
                        send_data(); //发送数据
                        Serial.println("本次数据发送完成");
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

void calsleeptime()
{
    voltage = 4.2f * analogRead(A0) / 1024;

    if (voltage > 3.3)
        sleepsec = 300 + (4.2 - voltage) * 1500 / 0.9; //3.3V-4.2V，平均延时为1800秒 - 300秒（30分-5分)
    else
        sleepsec = 70 * 60; //延时70分钟，最长延时为71分钟
}

void setup()
{
    pinMode(2, OUTPUT);           //GPIO为LED
    digitalWrite(LED_BUILTIN, 0); //点亮

    Serial.begin(115200); //配置串口
}

void loop()
{
    calsleeptime();
    if (voltage > 3.3)
    {
        WiFisetup(); //自动配网

        if (loginAPRS() == false)
            sleepsec = 30;
        client.stop();
    }
    else
        Serial.println("警告：当前电压低于3.3V，停止数据处理");
    digitalWrite(LED_BUILTIN, 1); //关灯

#ifdef DEBUG_MODE
    delay(10 * 1000);
    digitalWrite(LED_BUILTIN, 0); //亮灯
#else
    ESP.deepSleep(sleepsec * 1000 * 1000); //休眠
#endif
}
