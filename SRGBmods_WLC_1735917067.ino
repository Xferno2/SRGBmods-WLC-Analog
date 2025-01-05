// SRGBmods Wifi LED Controller Firmware
// generated at https://srgbmods.net


// You can edit your network credentials below!
const char ssid[] = "";
const char pass[] = "";
// DO NOT EDIT BELOW THIS LINE UNLESS YOU KNOW WHAT YOU ARE DOING!


#include <ArduinoJson.h>
#include <EEPROM.h>
#include <Adafruit_NeoPXL8.h>
#if defined ARDUINO_RASPBERRY_PI_PICO_W
	#include <WiFi.h>
	#include <ESP8266mDNS.h>
	#include <AsyncWebServer_RP2040W.h>
	#include <AsyncUDP_RP2040W.h>
	const char mcu[] = "PicoW";
	AsyncUDP udp;
	AsyncWebServer server(80);
#elif defined ARDUINO_NANO_RP2040_CONNECT
	#include <WiFiNINA.h>
	const char mcu[] = "NanoConnect";
	WiFiUDP udp;
	WiFiServer server(80);
	String HTTP_req;
#else
	#error Please select a supported MCU!
#endif

const char firmware[] = "0.2.0";
String hostname;
IPAddress ip;
JsonDocument doc;
String jsonInfo;
String mac_string;
unsigned long lastDeviceCheck;
const int ledsPerPacket = 480;
#define MAX_PINS 8
#define PACKET_SIZE 1444

const unsigned int port = 1337;

const int numPins = 1;
int8_t pinList[MAX_PINS] = { 0,-1,-1,-1,-1,-1,-1,-1 };
const int ledsPerStrip = 1;
const int ledsPerPin[numPins] = { 1 };
const int offsetPerPin[numPins] = { 1 };
const int totalLedCount = 1;

const int r= 22;
const int g= 21;
const int b= 20;

int packetCount = 0;
int deviceCount = 0;
int ledCounter = 0;
bool updateChannel = false;
bool isTurnedOn = true;
bool requestTurnOff = false;
Adafruit_NeoPXL8 leds(ledsPerStrip, pinList, NEO_GRB);

byte udpPacket[PACKET_SIZE];

const uint8_t BrightFull = 255;
bool newUDPpacketArrived = false;
const byte rainbowColors[7][3] = { { 255, 0, 0 }, { 255, 37, 0 }, { 255, 255, 0 }, { 0, 128, 0 }, { 128, 128, 0 }, { 0, 0, 200 }, { 75, 0, 130 } };
unsigned long lastPacketRcvd;
bool DataLedOn = false;
bool hardwareLighting = false;
unsigned long lastHWLUpdate;
const int eeprom_HWL_enable = 0;
const int eeprom_HWL_return = 1;
const int eeprom_HWL_returnafter = 2;
const int eeprom_HWL_effectMode = 3;
const int eeprom_HWL_effectSpeed = 4;
const int eeprom_HWL_brightness = 5;
const int eeprom_HWL_color_r = 6;
const int eeprom_HWL_color_g = 7;
const int eeprom_HWL_color_b = 8;
const int eeprom_StatusLED_enable = 9;
byte HWL_enable;
byte HWL_return;
byte HWL_returnafter;
byte HWL_effectMode;
byte HWL_effectSpeed;
byte HWL_brightness;
byte HWL_singleColor[3];
byte StatusLED_enable;

int WiFiStatus = WL_DISCONNECTED;

bool Core0ready = false;

void setup()
{
  pinMode(r, OUTPUT);
  pinMode(g,OUTPUT);
  pinMode(b, OUTPUT);
	#if defined ARDUINO_RASPBERRY_PI_PICO_W
		mac_string = WiFi.macAddress();
		mac_string.replace(":","");
	#elif defined ARDUINO_NANO_RP2040_CONNECT
		HTTP_req = "";
		byte mac[6];
		WiFi.macAddress(mac);
		mac_string = String(mac[5], HEX) + String(mac[4], HEX) + String(mac[3], HEX) + String(mac[2], HEX) + String(mac[1], HEX) + String(mac[0], HEX);
	#endif
	hostname = "SRGBmods-WLC-";
	hostname.concat(mac_string.substring(6, 12));
	WiFi.setHostname(hostname.c_str());
	while(WiFiStatus != WL_CONNECTED)
	{
		#if defined STATIC_IP_CONFIG
			WiFi.config(ip_rx, dns, gateway, subnet);
		#endif
		WiFiStatus = WiFi.begin(ssid, pass);
		delay(10000);
	}
	delay(500);
	createJSONdeviceInfo();
	#if defined ARDUINO_RASPBERRY_PI_PICO_W
		createMDNSservice();
		udp.onPacket([](AsyncUDPPacket packet)
		{
			parsePacket(packet);
		});
		udp.listen(port);
		server.on("/", HTTP_GET, [](AsyncWebServerRequest * request)
		{
			handleJSON(request);
		});
		server.on("/turnon", HTTP_GET, [](AsyncWebServerRequest * request)
		{
			handleTurnOn_request(request);
		});
		server.on("/turnoff", HTTP_GET, [](AsyncWebServerRequest * request)
		{
			handleTurnOff_request(request);
		});
		server.onNotFound(handleNotFound);
	#else
		udp.begin(port);
		delay(500);
	#endif
	server.begin();
	delay(500);
	Core0ready = true;
}

void setup1()
{
	while(!Core0ready) delay(1);
	pinMode(LED_BUILTIN, OUTPUT);
	EEPROM.begin(256);
	HWL_readEEPROM();
	leds.begin();
	delay(500);
}

void loop()
{
	#if defined ARDUINO_RASPBERRY_PI_PICO_W
		MDNS.update();
	#else
		checkUDPpackets();
		runWebserver();
	#endif
	unsigned long currentMillis = millis();
	if(currentMillis - lastDeviceCheck >= 10000)
	{
		lastDeviceCheck = currentMillis;
		checkDeviceStatus();
	}
}

void loop1()
{
	if(isTurnedOn)
	{
		if(newUDPpacketArrived)
		{
			processUDPpacket();
			newUDPpacketArrived = false;
		}
		if(updateChannel)
		{
			updateLighting();
		}
		if(millis() - lastPacketRcvd >= 500 && DataLedOn)
		{
			DataLedOn = false;
			toggleOnboardLED(false);
		}
		handleHWLighting();
	}
	else
	{
		if(DataLedOn)
		{
			DataLedOn = false;
			toggleOnboardLED(false);
		}
	}
	if(requestTurnOff)
	{
		resetLighting();
	}
}

#if defined ARDUINO_RASPBERRY_PI_PICO_W
	void parsePacket(AsyncUDPPacket packet)
	{
		if(isTurnedOn)
		{
			memcpy(udpPacket, packet.data(), PACKET_SIZE);
			newUDPpacketArrived = true;
		}
		else
		{
			packet.flush();
		}
	}

	void handleJSON(AsyncWebServerRequest *request)
	{
		request->send(200, "application/json", jsonInfo);
	}
	
	void handleTurnOn_request(AsyncWebServerRequest *request)
	{
		String message = "Turn ON!\n";
		request->send(200, "text/plain", message);
		handleTurnOn();
	}
	
	void handleTurnOff_request(AsyncWebServerRequest *request)
	{
		String message = "Turn OFF!\n";
		request->send(200, "text/plain", message);
		handleTurnOff();
	}

	void handleNotFound(AsyncWebServerRequest *request)
	{
		String message = "File Not Found\n";
		request->send(404, "text/plain", message);
	}
	
	void createMDNSservice()
	{
		MDNS.begin(hostname, ip, 60);
		MDNS.addService("srgbmods-wlc", "tcp", 80);
		MDNS.addServiceTxt("srgbmods-wlc", "tcp", "mac", doc["mac"]);
		MDNS.addServiceTxt("srgbmods-wlc", "tcp", "ip", doc["ip"]);
		MDNS.addServiceTxt("srgbmods-wlc", "tcp", "name", doc["name"]);
		MDNS.addServiceTxt("srgbmods-wlc", "tcp", "product", doc["product"]);
	}
#else
	void checkUDPpackets()
	{
		if(isTurnedOn)
		{
			if(!newUDPpacketArrived)
			{
				if(udp.parsePacket() > 0)
				{
					udp.read(udpPacket, PACKET_SIZE);
					newUDPpacketArrived = true;
				}
			}
		}
		else
		{
			newUDPpacketArrived = false;
			udp.flush();
		}
	}
	
	void runWebserver()
	{
		WiFiClient client = server.available();
		if (client)
		{
			bool currentLineIsBlank = true;
			while (client.connected())
			{
				if (client.available())
				{
					char c = client.read();
					HTTP_req += c;
					if (c == '\n' && currentLineIsBlank)
					{
						client.println("HTTP/1.1 200 OK");
						if(HTTP_req.startsWith("GET /turnon"))
						{
							client.println("Content-Type: text/plain");
							client.println("Connection: close");
							client.println();
							client.println("Turn ON!");
							handleTurnOn();
							break;
						}
						else if(HTTP_req.startsWith("GET /turnoff"))
						{
							client.println("Content-Type: text/plain");
							client.println("Connection: close");
							client.println();
							client.println("Turn OFF!");
							handleTurnOff();
							break;
						}
						else
						{
							client.println("Content-Type: application/json");
							client.println("Connection: close");
							client.println();
							client.println(jsonInfo);
							break;
						}
					}
					if (c == '\n')
					{
						currentLineIsBlank = true;
					}
					else if (c != '\r')
					{
						currentLineIsBlank = false;
					}
				}
			}
			client.stop();
			HTTP_req = "";
		}
	}
#endif

void handleTurnOn()
{
	if(!isTurnedOn)
	{
		resetLighting();
		isTurnedOn = true;
	}
}

void handleTurnOff()
{
	if(isTurnedOn)
	{
		isTurnedOn = false;
		resetLighting();
	}
}

void HWL_readEEPROM()
{
	HWL_enable = EEPROM.read(eeprom_HWL_enable) == 0xFF ? 0x01 : EEPROM.read(eeprom_HWL_enable);
	HWL_return = EEPROM.read(eeprom_HWL_return) == 0xFF ? 0x01 : EEPROM.read(eeprom_HWL_return);
	HWL_returnafter = EEPROM.read(eeprom_HWL_returnafter) == 0xFF ? 0x0A : EEPROM.read(eeprom_HWL_returnafter);
	HWL_effectMode = EEPROM.read(eeprom_HWL_effectMode) == 0xFF ? 0x01 : EEPROM.read(eeprom_HWL_effectMode);
	HWL_effectSpeed = EEPROM.read(eeprom_HWL_effectSpeed) == 0xFF ? 0x06 : EEPROM.read(eeprom_HWL_effectSpeed);
	HWL_brightness = EEPROM.read(eeprom_HWL_brightness) == 0xFF ? 0x7F : EEPROM.read(eeprom_HWL_brightness);
	HWL_singleColor[0] = EEPROM.read(eeprom_HWL_color_r);
	HWL_singleColor[1] = EEPROM.read(eeprom_HWL_color_g);
	HWL_singleColor[2] = EEPROM.read(eeprom_HWL_color_b);
	StatusLED_enable = EEPROM.read(eeprom_StatusLED_enable) == 0xFF ? 0x00 : EEPROM.read(eeprom_StatusLED_enable);
	if(!HWL_enable)
	{
		hardwareLighting = false;
		isTurnedOn = false;
	}
	else
	{
		isTurnedOn = true;
		hardwareLighting = true;
	}
}

void toggleOnboardLED(bool state)
{
	if(StatusLED_enable || state == false)
	{
		digitalWrite(LED_BUILTIN, state ? HIGH : LOW);
	}
}

void updateLighting()
{
	if(!DataLedOn)
	{
		DataLedOn = true;
		toggleOnboardLED(true);
	}
	packetCount = 0;
	deviceCount = 0;
	ledCounter = 0;
	updateChannel = false;
	// if(leds.canShow())
	// {
	// 	leds.show();
	// }
}

void HWLfillRainbow(uint16_t first_hue = 0, int8_t reps = 1, uint8_t saturation = 255, uint8_t brightness = 255, bool gammify = false)
{
	for(int ledIdx = 0; ledIdx < ledsPerStrip * numPins; ledIdx++)
	{
		uint16_t hue = first_hue + (ledIdx * reps * 65536) / ledsPerStrip;
		uint32_t color = leds.ColorHSV(hue, saturation, brightness);
		if (gammify) color = leds.gamma32(color);
    uint8_t colorArray[4] = {0x1,0x2,0x3,0x4};
    //disassambleUint32(colorArray,color);
    //setPixel(colorArray[0],colorArray[1],colorArray[2]);
	}
	// if(leds.canShow())
	// {
	// 	leds.show();
	// }
}

void HWLfillSolid(byte r, byte g, byte b, byte brightness = BrightFull)
{
	for(int ledIdx = 0; ledIdx < ledsPerStrip * numPins; ledIdx++)
	{
		//leds.setPixelColor(ledIdx, setRGBbrightness(r, g, b, brightness));
    uint8_t colorArray[4] = {0x1,0x2,0x3,0x4};
    //disassambleUint32(colorArray,setRGBbrightness(r, g, b, brightness));
    //setPixel(colorArray[0],colorArray[1],colorArray[2]);
	}
	// if(leds.canShow())
	// {
	// 	leds.show();
	// }
}

void handleHWLighting()
{
	unsigned long currentMillis = millis();
	if(HWL_enable == 1 && hardwareLighting == true)
	{
		int hwleffectspeed = ceil(300 / HWL_effectSpeed);
		if(currentMillis - lastHWLUpdate >= (hwleffectspeed < 10 ? 10 : hwleffectspeed))
		{
			lastHWLUpdate = currentMillis;
			switch(HWL_effectMode)
			{
				case 1:
				{
					static uint16_t firsthue = 0;
					HWLfillRainbow(firsthue -= 256, 10, 255, HWL_brightness > 75 ? HWL_brightness : 75);
					return;
				}
				case 2:
				{
					static int currentColor = 0;
					static uint8_t breath_bright = HWL_brightness;
					static bool isDimming = true;
					if(isDimming && (breath_bright-1 <= 1))
					{
						if((currentColor + 1) > 6)
						{
							currentColor = 0;
						}
						else
						{
							currentColor++;
						}
						isDimming = false;
					}
					else if(!isDimming && (breath_bright+1 >= HWL_brightness))
					{
						isDimming = true;
					}
					breath_bright = isDimming ? breath_bright-1 : breath_bright+1;
					HWLfillSolid(rainbowColors[currentColor][0], rainbowColors[currentColor][1], rainbowColors[currentColor][2], breath_bright);
					return;
				}
				case 3:
				{
					if(!HWL_singleColor[0] && !HWL_singleColor[1] && !HWL_singleColor[2])
					{
						HWLfillSolid(0x00, 0x00, 0x00);
						return;
					}
					HWLfillSolid(HWL_singleColor[0], HWL_singleColor[1], HWL_singleColor[2], HWL_brightness);
					return;
				}
				case 4:
				{
					static uint8_t breath_bright = HWL_brightness;
					static bool isDimming = true;
					isDimming = (isDimming && (breath_bright-1 <= 1)) ? false : (!isDimming && (breath_bright+1 >= HWL_brightness)) ? true : isDimming;
					breath_bright = isDimming ? breath_bright-1 : breath_bright+1;
					HWLfillSolid(HWL_singleColor[0], HWL_singleColor[1], HWL_singleColor[2], breath_bright);
					return;
				}
			}
			return;
		}
	}
	if(HWL_enable == 1 && hardwareLighting == false && HWL_return == 1)
	{
		if(currentMillis - lastPacketRcvd >= (HWL_returnafter * 1000))
		{
			resetLighting();
			hardwareLighting = true;
			return;
		}
	}
}

unsigned int setRGBbrightness(byte r, byte g, byte b, byte brightness)
{
	r = (r * brightness) >> 8;
	g = (g * brightness) >> 8;
	b = (b * brightness) >> 8;
	return(((unsigned int)r & 0xFF )<<16 | ((unsigned int)g & 0xFF)<<8 | ((unsigned int)b & 0xFF));
}

void resetLighting()
{
	newUDPpacketArrived = false;
	packetCount = 0;
	deviceCount = 0;
	ledCounter = 0;
	updateChannel = false;
	leds.clear();
	// if(leds.canShow())
	// {
	// 	requestTurnOff = false;
	// 	leds.show();
	// }
	// else
	// {
	// 	requestTurnOff = true;
	// }
}

void setPixel(uint8_t _r, uint8_t _g, uint8_t _b){
    analogWrite(r, _r);
    analogWrite(g, _g);
    analogWrite(b, _b);
}

void disassambleUint32(uint8_t * arr, uint32_t target){
    arr[3] = (uint8_t)target;
    arr[2] = (uint8_t)(target>>=8);
    arr[1] = (uint8_t)(target>>=16);
    arr[0] = (uint8_t)(target>>=24);
}

void processUDPpacket()
{
	if(udpPacket[0] && udpPacket[1] && !udpPacket[2] && udpPacket[3] == 0xAA)
	{
		lastPacketRcvd = millis();
		if(HWL_enable == 1 && hardwareLighting == true)
		{
			resetLighting();
			hardwareLighting = false;
		}
		colorLeds();
	}
	else if(!udpPacket[0] && !udpPacket[1] && !udpPacket[2] && udpPacket[3] == 0xBB)
	{
		HWL_enable = udpPacket[4];
		HWL_return = udpPacket[5];
		HWL_returnafter = udpPacket[6];
		HWL_effectMode = udpPacket[7];
		HWL_effectSpeed = udpPacket[8];
		HWL_brightness = udpPacket[9];
		HWL_singleColor[0] = udpPacket[10];
		HWL_singleColor[1] = udpPacket[11];
		HWL_singleColor[2] = udpPacket[12];
		StatusLED_enable = udpPacket[13];
		
		if(HWL_enable)
		{
			hardwareLighting = true;
		}
		else
		{
			hardwareLighting = false;
		}
		
		DataLedOn = false;
		toggleOnboardLED(false);
		
		EEPROM.write(eeprom_HWL_enable, HWL_enable);
		EEPROM.write(eeprom_HWL_return, HWL_return);
		EEPROM.write(eeprom_HWL_returnafter, HWL_returnafter);
		EEPROM.write(eeprom_HWL_effectMode, HWL_effectMode);
		EEPROM.write(eeprom_HWL_effectSpeed, HWL_effectSpeed);
		EEPROM.write(eeprom_HWL_brightness, HWL_brightness);
		EEPROM.write(eeprom_HWL_color_r, HWL_singleColor[0]);
		EEPROM.write(eeprom_HWL_color_g, HWL_singleColor[1]);
		EEPROM.write(eeprom_StatusLED_enable, StatusLED_enable);
		
		EEPROM.commit();
		delay(10);
		resetLighting();
	}
}

void colorLeds()
{
	if(udpPacket[0] == packetCount+1)
	{
		int ledsSent = packetCount * ledsPerPacket;
		if(ledsSent < totalLedCount)
		{	
			for(int ledIdx = 0; ledIdx < ledsPerPacket; ledIdx++)
			{
				if(ledsSent + 1 <= totalLedCount)
				{
					if((packetCount * ledsPerPacket) + ledIdx >= offsetPerPin[deviceCount])
					{
						deviceCount++;
						ledCounter = 0;
					}
					//leds.setPixelColor((deviceCount * ledsPerStrip) + ledCounter, udpPacket[4+(ledIdx*3)], udpPacket[4+(ledIdx*3)+1], udpPacket[4+(ledIdx*3)+2]);
          setPixel(udpPacket[4+(ledIdx*3)],udpPacket[4+(ledIdx*3)+1],udpPacket[4+(ledIdx*3)+2]);
          ledsSent++;
					ledCounter++;
				}
			}
		}
		packetCount++;
	}
	else
	{
		packetCount = 0;
		deviceCount = 0;
		ledCounter = 0;
	}
	if(packetCount == udpPacket[1])
	{
		updateChannel = true;
	}
	return;
}

void checkDeviceStatus()
{
	WiFiStatus = WiFi.status();
	if(WiFiStatus != WL_IDLE_STATUS && WiFiStatus != WL_CONNECTED)
	{
		WiFi.disconnect();
		#if defined STATIC_IP_CONFIG
			WiFi.config(ip_rx, dns, gateway, subnet);
		#endif
		WiFiStatus = WiFi.begin(ssid, pass);
	}
	else
	{
		updateSignalStrength();
	}
}

void createJSONdeviceInfo()
{
	jsonInfo = "";
	ip = WiFi.localIP();
	String ip_string = ip.toString();
	int signalStrength = getSignalStrength(WiFi.RSSI());
	doc["product"] = "SRGBmods-WLC";
	doc["firmware"] = firmware;
	doc["mcu"] = mcu;
	doc["name"] = hostname;
	doc["mac"] = mac_string;
	doc["udpport"] = port;
	doc["ip"] = ip_string;
	doc["signal"] = signalStrength;
	doc["ledcount"] = totalLedCount;
	serializeJson(doc, jsonInfo);
}

void updateSignalStrength()
{
	jsonInfo = "";
	int signalStrength = getSignalStrength(WiFi.RSSI());
	doc["signal"] = signalStrength;
	serializeJson(doc, jsonInfo);
}

int getSignalStrength(int rssi)
{
	int quality = 0;
	if (rssi <= -100)
	{
		quality = 0;
	}
	else if (rssi >= -50)
	{
		quality = 100;
	}
	else
	{
		quality = 2 * (rssi + 100);
	}
	return quality;
}