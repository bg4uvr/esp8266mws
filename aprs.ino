// ESP8266 APRS 气象站

//#define DEBUG_MODE  //调试模式时不把语句发往服务器
//#define HUMIDITY  //不使用湿度时注释掉本句（ESP-01S没有多余IO使用湿度）
#define SEND_INTERVAL 5 * 60 * 1000 //发送数据间隔（毫秒）
#define RECV_INTERVAL 30 * 1000     //接收心跳包的间隔， aprsc 2.1.5 服务器大约为20秒

#include <ESP8266WiFi.h>
#include <Wire.h>
#include <Time.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <Adafruit_BMP280.h>

#ifdef HUMIDITY
#include "DHTesp.h"
#endif

WiFiClient client;                    //初始化WiFiclient实例
const char *host = "china.aprs2.net"; //APRS服务器地址
const int port = 14580;               //APRS服务器端口
const char *logininfo =
  "user BG4UVR-10 pass 21410 vers esp-01s 0.1 filter m/10\r\n"; //APRS登录命令
char senddata[150] = {0};                                         //APRS数据缓存
bool auth = false;                                                //APRS验证状态

Adafruit_BMP280 bmp; //初始化BMP280实例

#ifdef HUMIDITY
DHTesp dht; //DHT11实例
#endif

uint32_t last_send;
uint32_t last_recv;
uint32_t data_cnt;

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

void setup()
{
  Serial.begin(115200); //配置串口
  WiFisetup();          //自动配网
  Otasetup();           //OTA更新

#ifdef HUMIDITY
  dht.setup(5, DHTesp::DHT11); // Connect DHT sensor to GPIO 5
#endif

  Serial.println("正在初始化BMP280传感器...");
  if (!bmp.begin(BMP280_ADDRESS_ALT))
  {
    Serial.println(F("未找到BMP280传感器，请检查接线以及设置正确i2c地址(0x76 或 0x77)。"));
    Serial.println(F("系统当前已停止..."));
    while (1)
      ArduinoOTA.handle(); //系统停止执行，只扫描OTA任务
  }

  //设置BMP280采样参数
  bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                  Adafruit_BMP280::SAMPLING_X2,
                  Adafruit_BMP280::SAMPLING_X16,
                  Adafruit_BMP280::FILTER_X16,
                  Adafruit_BMP280::STANDBY_MS_500);

  Serial.println("BMP280传感器初始化成功");
}

//发送数据
void send_data()
{
#ifdef HUMIDITY
  int humidity = dht.getHumidity();
  if (humidity > 100)
    humidity = 0;
#endif

  float temperature = bmp.readTemperature();
  float pressure = bmp.readPressure();

#ifdef HUMIDITY
  Serial.printf("湿度：%d\t温度：%0.2f\t气压：%0.2f\r\n", humidity, temperature, pressure);
#else
  Serial.printf("温度：%0.2f\t气压：%0.2f\r\n", temperature, pressure);
#endif

  int temperaturef = temperature * 9 / 5 + 32; //转换成华氏度
  int pressure_int = pressure / 10;            //把气压浮点数的Pa值转换成0.1hPa的整形数值

#ifdef HUMIDITY
  snprintf(senddata, sizeof(senddata),
           "BG4UVR-10>APZESP,qAC,:=3153.47N/12106.86E_c000s000g000t%03dr000p000h02db%05d send_cnt:%d, runtime:%ds\r\n",
           temperaturef, humidity, pressure_int, ++data_cnt, millis() / (1000));
#else
  snprintf(senddata, sizeof(senddata),
           "BG4UVR-10>APZESP,qAC,:=3153.47N/12106.86E_c000s000g000t%03dr000p000h50b%05d send_cnt:%d, runtime:%ds\r\n",
           temperaturef, pressure_int, ++data_cnt, millis() / (1000));
#endif

#ifndef DEBUG_MODE
  client.print(senddata); //向服务器反馈信息
#endif
  last_send = millis();
  Serial.println(senddata);
}

void loop()
{
  //如果尚未建立APRS服务器连接
  if (!client.connected())
  {
    Serial.println(F("APRS服务器未连接，正在连接..."));
    if (client.connect(host, port))
    {
      last_recv = millis();
      Serial.println(F("APRS服务器已连接"));
    }
    else
    {
      Serial.println(F("APRS服务器连接失败，稍后重试"));
      delay(5000);
    }
  }
  //接收超时主动断开连接
  else if (millis() - last_recv > RECV_INTERVAL)
  {
    auth = false;
    client.stop();
    Serial.println(F("APRS服务器数据接收超时，已断开"));
  }

  //如果缓冲区字符串大于0
  if (client.available())
  {
    last_recv = millis();                       //更新最后接收数据时间
    String line = client.readStringUntil('\n'); //获取字符串
    Serial.println(line);                       //把字符串传给串口

    if (auth == false)
    {
      if (line.indexOf("javAPRSSrvr") != -1) // !=-1含有 ==-1不含有
      {
        Serial.println("javAPRSSrvr");
      }
      else if (line.indexOf("aprsc") != -1)
      {
        Serial.println(F("正在登录ARPS服务器..."));
        Serial.println(logininfo);
        client.print(logininfo); //向服务器反馈登录信息
      }
      else if (line.indexOf("verified") != -1)
      {
        auth = true;            //验证成功
        send_data();
        Serial.println(F("APRS服务器登录验证已通过"));
      }
    }
  }

  //在已验证情况下，每间隔定时周期，发送一次数据
  if (auth == true && millis() - last_send >= SEND_INTERVAL)
    send_data();

  //扫描OTA任务
  ArduinoOTA.handle();
}
