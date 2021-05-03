// ESP8266 APRS 气象站

#include <ESP8266WiFi.h>
#include <Wire.h>
#include <Ticker.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <Adafruit_BMP280.h>

WiFiClient client;                                                                     //初始化WiFiclient实例
bool auth = false;                                                                     //APRS验证状态
bool connect_wifi = false;                                                             //WiFi连接状态
const char *host = "china.aprs2.net";                                                  //APRS服务器地址
const int port = 14580;                                                                //APRS服务器端口
const char *logininfo = "user BG4UVR-10 pass 21410 vers esp-01s 0.1 filter m/50\r\n";  //APRS登录命令
char senddata[150] = {0};                                                              //APRS数据缓存
bool all_ok = false;                                                                   //所有发送的条件都已具备

Adafruit_BMP280 bmp; //初始化BMP280实例
Ticker tker;         //初始化定时器实例

//自动配网
void WiFisetup()
{
  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //wifiManager.resetSettings();

  //wifiManager.setAPStaticIPConfig(IPAddress(10, 0, 1, 1), IPAddress(10, 0, 1, 1), IPAddress(255, 255, 255, 0));
  //wifiManager.autoConnect("Wifi Clock");
  //wifiManager.setTimeout(180);

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
  WiFi.setAutoConnect(true); // 设置自动连接
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
  ArduinoOTA.setPassword("21232f297a57a5a743894a0e4a801fc3");

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

  WiFisetup(); //自动配网
  Otasetup();  //OTA更新

  connect_wifi = true;

  Serial.println("");
  Serial.println("正在初始化BMP280传感器...");

  if (!bmp.begin(BMP280_ADDRESS_ALT))
  {
    Serial.println(F("未找到BMP280传感器，请检查接线以及设置正确i2c地址(0x76 或 0x77)。"));
    while (1)
      delay(10);
  }

  //设置BMP280采样参数
  bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                  Adafruit_BMP280::SAMPLING_X2,
                  Adafruit_BMP280::SAMPLING_X16,
                  Adafruit_BMP280::FILTER_X16,
                  Adafruit_BMP280::STANDBY_MS_500);

  Serial.println("BMP280传感器初始化成功");

  //每10分钟发送数据
  tker.attach(5 * 60, data_flush);
}

void data_flush()
{
  if (all_ok)
  {
    float temperature = bmp.readTemperature();
    int pressure = bmp.readPressure() / 10; //把气压浮点数的Pa值转换成0.1hPa的整形数值

    Serial.printf("温度：");
    Serial.print(temperature);
    Serial.print('\t');
    Serial.printf("气压：");
    Serial.println(pressure);

    int temperaturef = temperature * 9 / 5 + 32; //转换成华氏度

    snprintf(senddata, sizeof(senddata), "BG4UVR-10>APESP,qAS,:=3153.47N/12106.86E_c000s000g000t%03dr000p000h00b%05d esp-01s + bmp280\r\n", temperaturef, pressure);
    client.print(senddata); //向服务器反馈信息

    Serial.println(senddata);
  }
}

void loop()
{
  //如果未连接APRS服务器
  if (!client.connected())
  {
    auth = false;
    Serial.println(F("APRS服务器未连接，正在连接..."));
    if (client.connect(host, port))
      Serial.println(F("APRS服务器已连接"));
  }

  if (client.available()) //如果缓冲区字符串大于0
  {
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
        //验证成功
        Serial.println(F("APRS服务器登录验证已通过"));
        auth = true;
        all_ok = true;
        data_flush();
      }
    }
  }
  ArduinoOTA.handle();
}
