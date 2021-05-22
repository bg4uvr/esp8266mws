# Esp8266 Mini Weather Station

The English instructions at the bottom



# ESP8266 迷你气象站



#### 关于这个小DIY的简单介绍

这个小制作是我学习esp8266 arduino的第一个小实验，它使用常见的esp8266模块（如NODEMCU、WEMOS等），外接两只I2C总线的气压温度湿度传感器，来实现了**简单的**业余无线电爱好者使用的APRS气象站功能（说是简单是因为它并没有风向、风速和雨量功能，而且后期也没有加入这些功能的打算，原因是这类传感器价格比较贵，不太适应瞎折腾玩 :-p）。实现类似功能的开源小制作估计也有不少，相比较而言，我这个的最主要特点以下几个：

1. **简单**

   整个制作主要使用了3个模块，CN3791太阳能充电模块、NODEMCU板、ATH20+BMP280模块。

2. **省电**

   由于让ESP8266工作于休眠间隙状态，并且使用了高效的太阳能充电控制模块，所以它仅使用一片标称6V/1.2W的小太阳能电池板来供电（其实更小的也可以），外加一只18650锂电池来储能，就基本上可以实现全候不间断工作。我的整个装置放在房子的北侧，除了夏季的清晨和傍晚，阳光都无法直射，装置只靠天空的散射光即可充分充电，即使是阴天也是如此。

3. **便宜**

   我实际制作的总价：ESP8266模块10.58元  + 1.2W太阳能板9.9元 + 18650(1200mAh)2.9元 + 18650电池盒1.3元 + AHT20+BMP280传感器模块10元 + 塑料防水外壳10元 + CN3791太阳能充电模块12.3元 + 锂电保护板1.04元 = **58.02元**。

4. **配置方便**

   使用网络调试工具，通过命令行的方式可以配置全部的工作参数。

   

**我自己制作小气象站的运行状态可以在这里看到**

[BG4UVR-13 的APRS/CWOP气象报告 – Google Maps APRS](https://aprs.fi/weather/a/BG4UVR-13)



#### 写在前面的重要提示

**为保护APRS网络的正常运行秩序，在此特别声明及提醒注意以下几点：**

- **本制作仅适合有合法业余电台呼号的业余无线电爱好者参考制作，如果您不符合此条件，则代码仅供参考，一定不可以实际制作安装使用。**
- **这个制作在使用的时候，需要设置相关的APRS服务器地址、端口、个人呼号和验证码，这些信息请自行准备，我不提供关于此方面的信息。**
- **并且如果您基于本代码重新修改发布您自己的作品时，也强烈建议千万不可将上述信息内置于您的代码中。**



#### 电路硬件连接

由于电路结构非常简单，所以我不准备专业画图，直接用文字说明好了。

1. **供电电源**

   太阳能电池接入CN3791充电模块，充电模块的输出连接锂电保护板和18650电池，保护板的输出直接接在NODEMCU板的3.3V电源和GND地上（虽然esp8266的官方不建议锂电直接供电，但权衡利弊后我觉得这样接最合适了）。

2. **传感器**

   我的AHT20+BMP280模块是一体的，但即使使用两个单独的模块也一样，因为I2C总线本身就是支持多设备的，SDA接GPIO12（D6），SCL接GPIO14（D5）。**需要注意的是，BMP280模块，根本硬件不同，可以有两种硬件地址，如果你的代码无法检测到它，请把代码中bmp.begin()更改为bmp.begin(BMP280_ADDRESS_ALT)。**

3. **电池电压检测**

   由于是使用esp8266 ADC的VCC检测方式，所以需要拆除NODEMCU板A0脚上外接的两只电阻，一般是100K和220K的。

4. **休眠自动唤醒**

   GPIO16（D0）通过470欧左右的电阻连接到RST脚，使用电阻的目的是保护IO端口，以免在意外的情况下烧毁IO管脚。



#### 常见问题

- 待补充~

  



### ENGLISH

因为我的英文不好，所以在阅读外国朋友的代码时特别麻烦，正因为如此，我特别能理解外国朋友看到中文介绍和代码注释时的感觉。所以我特别使用电脑翻译了全部的说明文字和代码的注释内容，以方便外国朋友。但我知道电脑翻译的正确性和准确性是极差的，所以英文的内容仅是无奈情况下的参考。同时也欢迎有能力的朋友能帮忙完善这些英文内容，可以通过发pull requests来更新维护，在此表示感谢~

Because my English is not good, it is very difficult for me to read the code of foreign friends. Because of this, I can understand the feeling of foreign friends when they see the Chinese introduction and code comments. So I specially use the computer to translate all the explanatory text and the code annotation content, for the convenience of foreign friends. But I know that the correctness and accuracy of computer translation is very poor, so the English content is only a helpless case of reference. We also welcome the ability of friends to help improve the English content, you can send pull requests to update maintenance, thank you~



#### A brief introduction to this little DIY

This small production is my first small experiment to learn ESP8266 Arduino. It uses common ESP8266 modules (such as Nodemcu, Wemos, etc.) and is connected with two pressure, temperature and humidity sensors of I2C bus.To implement the **simple** APRS weather station feature used by amateur radio operators (simple because it doesn't have wind, wind, and rainfall features, and there are no plans to add them in the future because they are expensive and not easy to play around with: -P).It is estimated that there are many small open source productions that can achieve similar functions. In comparison, the main features of this one are as follows:

1. **Simplicity**

   The whole production mainly uses three modules, CN3791 solar charging module, NODEMCU board, ATH20+BMP280 module.

2. **Save electricity**

   By keeping the ESP8266 in hibernation and using a highly efficient solar charging control module, it uses only a small 6V/1.2W solar panel to power it (or even smaller), plus an 18650 lithium-ion battery to store the energy, so it can operate almost continuously. My entire installation is on the north side of the house. Except in the summer mornings and evenings, there is no direct sunlight, and the device is fully charged by the scattered light from the sky, even on cloudy days.

3. **cheap**

   The total price of my actual production: ESP8266 module 10.58 yuan + 1.2W solar panel 9.9 yuan + 18650(1200mAh)2.9 yuan + 18650 battery box 1.3 yuan + AHT20+BMP280 sensor module 10 yuan + plastic waterproof shell 10 yuan +CN3791 solar charging module 12.3 yuan + lithium electric protection plate 1.04 yuan = **58.02 yuan(RMB)**

4. **Convenient configuration** 

   With the network debugging tool, you can configure all the working parameters from the command line.



**The operation status of my own small weather station can be seen here**

[BG4UVR-13 的APRS/CWOP气象报告 – Google Maps APRS](https://aprs.fi/weather/a/BG4UVR-13)



#### Important note written at the front



**In order to protect the normal operation order of the APRS network, we hereby declare and remind you of the following points:** 



- **This production is only suitable for legal amateur radio call sign amateur reference production, if you do not meet this condition, then the code is for reference only, must not actually make installation use.**

- **When using this production, you need to set the relevant APRS server address, port, personal call sign and verification code. Please prepare these information by yourself, I will not provide information about this.**

-  **And if you modify and distribute your own work based on this code, it is strongly recommended that you do not include the above information in your code.**








#### Circuit hardware connection



Because the structure of the circuit is very simple, I will not draw it professionally, but just use words to explain it.

1. **Power supply**

   The solar cell is connected to the CN3791 charging module, and the output of the charging module is connected to the lithium protection plate and the 18650 battery. The output of the protection plate is directly connected to the 3.3V power supply of the Nodemcu plate and the GND (although ESP8266 official does not recommend direct power supply from lithium, I think this connection is the most appropriate after weighing the advantages and disadvantages).

2. **sensor**

   My AHT20+BMP280 module is integrated, but even using two separate modules is the same, because the I2C bus itself supports multiple devices, with SDA connected to GPIO12 (D6) and SCL connected to GPIO14 (D5).**Note that the BMP280 module, with different hardware, can have two different hardware addresses. If your code cannot detect it, change the bmp.begin() in the code to bmp.begin(BMP280_ADDRESS_ALT).**

3. **battery voltage**

   As ESP8266 ADC VCC detection method is used, it is necessary to remove the two external resistors on the A0 pin of Nodemcu plate, generally 100K and 220K.

4. **Sleep automatically wake up**

   The GPIO16 (D0) is connected to the RST pin through a resistor of around 470 Ohm. The purpose of using resistors is to protect the IO port from accidental burning of the IO pin.



#### Frequently Asked Questions

- To be added ~



