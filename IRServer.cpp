#ifndef UNIT_TEST
#include <Arduino.h>
#endif

#include "IRServer.h"
#include "wifi_credentials.h"

#ifndef WIFI_SSID
#error "WIFI_SSID is not defined. Please create wifi_credentials.h with your WiFi credentials."
#endif

#ifndef WIFI_PASSWORD
#error "WIFI_PASSWORD is not defined. Please create wifi_credentials.h with your WiFi credentials."
#endif

#include <sstream>
#include <iostream>
#include <string>
#include <algorithm>

#include <Ticker.h> //Ticker Library

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <WiFiClient.h>

#include <OneWire.h>
#include <DallasTemperature.h>

#define CRITICAL_LOW_TEMPERATURE 0
#define CRITICAL_HI_TEMPERATURE 30
#define HOST_NAME "VC_Lab"

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;
MDNSResponder mdns;

ESP8266WebServer server(80);

#define IR_LED_BUS 0       // ESP8266 GPIO pin to use. Recommended: 4 (D2).
#define ONE_WIRE_BUS 2		 // Указываем, к какому выводу подключен датчик

IRsend irsend(IR_LED_BUS); // Set the GPIO to be used to sending the message.
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);

#define WATCH_DOG_PERIOD_MS 15000

Ticker watchDog(watchDogFunction, WATCH_DOG_PERIOD_MS, 0, MILLIS);

DeviceAddress warningLoSensorAddress;
DeviceAddress warningHiSensorAddress;

//Количество срабатываний сторожевого пса
int watchDogLoRaises = 0;
int watchDogHiRaises = 0;

//есть датчик с критически низкой температурой
bool isCriticalLoTemperatureSensor()
{
	uint8_t dsDevicesCount = DS18B20.getDeviceCount();
	DeviceAddress sA;

	// DS18B20.requestTemperatures();
	for (int i = 0; i < dsDevicesCount; i++)
	{
		float t = DS18B20.getTempCByIndex(i);
		if (t < CRITICAL_LOW_TEMPERATURE)
		{
			DS18B20.getAddress(sA, i);
			//std::copy((uint8_t *)warningSensorAddress, sizeof((uint8_t *)warningSensorAddress), (uint8_t*)sA);
			for (uint8_t i = 0; i < sizeof(warningLoSensorAddress); i++)
				warningLoSensorAddress[i] = sA[i];
			
			return true;
		}
	}
	for (uint8_t i = 0; i < sizeof(warningLoSensorAddress); i++)
		warningLoSensorAddress[i] = 0;
	
	return false;
}

//есть датчик с критически низкой температурой
bool isCriticalHiTemperatureSensor()
{
	uint8_t dsDevicesCount = DS18B20.getDeviceCount();
	DeviceAddress sA;

	// DS18B20.requestTemperatures();
	for (int i = 0; i < dsDevicesCount; i++)
	{
		float t = DS18B20.getTempCByIndex(i);
		if (t > CRITICAL_HI_TEMPERATURE)
		{
			DS18B20.getAddress(sA, i);
			for (uint8_t i = 0; i < sizeof(warningHiSensorAddress); i++)
				warningHiSensorAddress[i] = sA[i];
			
			return true;
		}
	}
	for (uint8_t i = 0; i < sizeof(warningLoSensorAddress); i++){
		warningLoSensorAddress[i] = 0;
	}
	return false;
}

void watchDogFunction()
{
	DS18B20.requestTemperatures();
	bool isTempLo = isCriticalLoTemperatureSensor();
	bool isTempHi = isCriticalHiTemperatureSensor();

	if (isTempHi){
		watchDogHiRaises++;
		IRSEND(rawDataAuxModeCool);
	}
	if (isTempLo){
		watchDogLoRaises++;
		IRSEND(rawDataAuxModeVent);
	}	
}

void ClearDeviceAddress(DeviceAddress adr){
	for (uint8_t i = 0; i < sizeof((uint8_t*)adr); i++){
		adr[i] = 0;
	}
}

bool isNullDeviceAddress(DeviceAddress adr){
	bool flag = true;
	for (uint8_t i = 0; i < sizeof((uint8_t*)adr); i++){
		if(adr[i]!=0){
			flag = false;
			break;
		}
	}
	return flag;
}

bool isEqualsDeviceAddress(DeviceAddress addr1, DeviceAddress addr2){
	for (uint8_t i = 0; i < sizeof(DeviceAddress); i++){
		if(addr1[i]!=addr2[i])
			return false;
	}
	return true;
}

// Вспомогательная функция для отображения адреса датчика ds18b20
std::string getSensorAddress(DeviceAddress deviceAddress)
{
	std::stringstream ss;
	for (uint8_t i = 0; i < 8; i++) {    
		ss << std::hex << ((int)deviceAddress[i]);
	}

	return ss.str();
}

std::string getSensData()
{
	int dsDevicesCount = DS18B20.getDeviceCount();
	DS18B20.requestTemperatures();

	std::stringstream ss;

	ss << "[";
	for (int i = 0; i < dsDevicesCount; i++){
		
		ss.precision(4);
		ss << "\"" << DS18B20.getTempCByIndex(i) << "\"" << ",";
	}
	ss.seekp(-1, ss.cur);ss << "]";
	return ss.str();
}

std::string getContentTemp()
{
	int dsDevicesCount = DS18B20.getDeviceCount();
	bool isParasiteMode = DS18B20.isParasitePowerMode();
	DeviceAddress sensorAddress;

	DS18B20.requestTemperatures();

	std::stringstream ss;

	ss << "<h2>Found [" << dsDevicesCount << "] temperature sensors in " << (isParasiteMode ? "PARASITE" : "NON parasite ") << "mode </h2>";

	for (int i = 0; i < dsDevicesCount; i++){
		DS18B20.getAddress(sensorAddress, i);
		bool isWarningSensor = isEqualsDeviceAddress(warningLoSensorAddress, sensorAddress);

		ss << (isWarningSensor ? R"(<h3 style="color: red; font-weight: bold;">)" : "<h3>");
		ss << "Temperature (" << i << ") [" << getSensorAddress(sensorAddress) << "] ";

		ss.precision(4);
		ss << "<span id=\"tmp" << i << "\">" << DS18B20.getTempCByIndex(i) << "</span></h3>";
	}

	ss << "<h4>Critical temperature: " << CRITICAL_LOW_TEMPERATURE << "</h4>";

	if (!isNullDeviceAddress(warningLoSensorAddress))
		ss << "<h4>Critical temperature sensor address:" << getSensorAddress(warningLoSensorAddress) << "</h4>";

	ss << "<h5>watchDog: [" << watchDog.state() <<"] remaining: " << watchDog.remaining() << "</h5>";
	ss << "<h5>watchDogRaises: Lo:[" << CRITICAL_LOW_TEMPERATURE << "]=" << watchDogLoRaises << " Hi:[" << CRITICAL_HI_TEMPERATURE << "]=" << watchDogHiRaises << "</h5>";

	return ss.str();
	}

	void handleRoot()
	{
	std::stringstream ss;

	ss << R"(<!DOCTYPE html><html>
<head><title>VC lab</title></head>
<body>
	<script src="https://code.jquery.com/jquery-3.6.0.min.js"></script>
	<script type="text/javascript">
	let updateTimeMs = 3000;

	var J = function(d, b) {
		var a = new XMLHttpRequest;
		a.open("GET", d, !0);
		a.responseType = "json";
		a.onload = function() {
			var c = a.status;
			200 === c ? b(null, a.response) : b(c, a.response)
		};
		try{a.send();}
		catch{b(null,null);}
	};

	function sensUpdateFunc(err, data){
		if (err !== null) {
			//alert('Something went wrong: ' + err);
		} else {
			$.each(data, function(i,v){
				$("#tmp"+i).text(v);
			});
		}
		setTimeout(updateFunc, updateTimeMs);
	}
	
	function updateFunc(){
		J("/sensData", sensUpdateFunc);
	}
	
	setTimeout(updateFunc, updateTimeMs);
	
	
	</script>
				)"
		 << getContentTemp()
		 << R"====(
	<p><a href="#" onClick="J('aux?autostart=', $.noop)">Aux AUTOSTART</a></p>
	<p><a href="#" onClick="J('aux?poweron=', $.noop)">Aux ON</a></p>
	<p><a href="#" onClick="J('aux?poweroff=', $.noop)">Aux OFF</a></p>
	<br/>
	<p><a href="#" onClick="J('aux?temp16=', $.noop)">Aux set Temp 16</a></p>
	<p><a href="#" onClick="J('aux?temp18=', $.noop)">Aux set Temp 18</a></p>
	<p><a href="#" onClick="J('aux?temp20=', $.noop)">Aux set Temp 20</a></p>
	<p><a href="#" onClick="J('aux?temp24=', $.noop)">Aux set Temp 24</a></p>
	<p><a href="#" onClick="J('aux?temp28=', $.noop)">Aux set Temp 28</a></p>
	<p><a href="#" onClick="J('aux?temp30=', $.noop)">Aux set Temp 30</a></p>
	<br/>
	<p><a href="#" onClick="J('aux?fanauto=', $.noop)">Aux set fan to AUTO</a></p>
	<p><a href="#" onClick="J('aux?fanmin=', $.noop)">Aux set fan to MIN</a></p>
	<p><a href="#" onClick="J('aux?fanmid=', $.noop)">Aux set fan to MID</a></p>
	<p><a href="#" onClick="J('aux?fanmax=', $.noop)">Aux set fan to MMAX</a></p>
	<br/>
	<p><a href="#" onClick="J('aux?modecooling=', $.noop)">Aux set COOLING mode</a></p>
	<p><a href="#" onClick="J('aux?modevent=', $.noop)">Aux set VENT mode</a></p>
	<br/>
	<p><a href="#" onClick="J('aux?display=', $.noop)">Aux set Display On/Off</a></p>
	<p></p>
	</body>
	</html>
	)====";
	server.send(200, "text/html", ss.str().c_str());
}

void handleIr()
{
	for (uint8_t i = 0; i < server.args(); i++)
	{
		if (server.argName(i) == "code")
		{
			uint32_t code = strtoul(server.arg(i).c_str(), NULL, 10);
#if SEND_NEC
			irsend.sendNEC(code, 32);
#endif // SEND_NEC
		}
		else if (server.argName(i) == "hexcode")
		{
			uint32_t code = strtoul(server.arg(i).c_str(), NULL, 16);
#if SEND_NEC
			irsend.sendNEC(code, 32);
#endif // SEND_NEC
		}
	}
	
	server.send(200, "text/html", "");
	//handleRoot();
}

void autostartAC()
{
	//turn AC ON
	IRSEND(rawDataAuxPowerOn);delay(500);
	//cooling mode
	IRSEND(rawDataAuxModeCool);delay(500);
	//fan to max
	IRSEND(rawDataAuxFanMax);delay(500);
	//temp to minimum
	IRSEND(rawDataAuxTemp16);
}

void handleAux()
{
	for (uint8_t i = 0; i < server.args(); i++)
	{
		if (server.argName(i) == "autostart"){ autostartAC(); }
		else if (server.argName(i) == "poweron"){ IRSEND(rawDataAuxPowerOn); }
		else if (server.argName(i) == "poweroff"){ IRSEND(rawDataAuxPowerOff); }
		else if (server.argName(i) == "temp16"){ IRSEND(rawDataAuxTemp16); }
		else if (server.argName(i) == "temp18"){ IRSEND(rawDataAuxTemp18); }
		else if (server.argName(i) == "temp20"){ IRSEND(rawDataAuxTemp20); }
		else if (server.argName(i) == "temp24"){ IRSEND(rawDataAuxTemp24); }
		else if (server.argName(i) == "temp28"){ IRSEND(rawDataAuxTemp28); }
		else if (server.argName(i) == "temp30"){ IRSEND(rawDataAuxTemp30); }
		else if (server.argName(i) == "fanmax"){ IRSEND(rawDataAuxFanMax); }
		else if (server.argName(i) == "fanmin"){ IRSEND(rawDataAuxFanMin); }
		else if (server.argName(i) == "fanmid"){ IRSEND(rawDataAuxFanMid); }
		else if (server.argName(i) == "fanauto"){ IRSEND(rawDataAuxFanAuto); }
		else if (server.argName(i) == "modecooling"){ IRSEND(rawDataAuxModeCool); }
		else if (server.argName(i) == "modevent"){ IRSEND(rawDataAuxModeVent); }
		else if (server.argName(i) == "display"){ IRSEND(rawDataAuxDisplay); }
	}

	server.send(200, "text/html", "");
}

void handleSendRaw()
{
	for (uint8_t i = 0; i < server.args(); i++)
	{
		if (server.argName(i) == "array")
		{
			uint32_t code = strtoul(server.arg(i).c_str(), NULL, 10);
			irsend.sendNEC(code, 32);
		}
	}
	server.send(200, "text/html", "");
}

void handleNotFound()
{
	std::stringstream ss;
	ss << "File Not Found\n\nURI: "
		<< server.uri().c_str()
		<< "\nMethod: "
		<< ((server.method() == HTTP_GET) ? "GET" : "POST")
		<< "\nArguments: "
		<< server.args()
		<< "\n";

	for (uint8_t i = 0; i < server.args(); i++)
		ss << " " << server.argName(i).c_str() << ": " << server.arg(i).c_str() << "\n";
	server.send(404, "text/plain", ss.str().c_str());
}

void setup(void)
{
	irsend.begin();

	Serial.begin(9600);
	WiFi.begin(ssid, password);
	Serial.println("");

	// Wait for connection
	while (WiFi.status() != WL_CONNECTED)
	{
		delay(500);
		Serial.print(".");
	}
	Serial.println("");
	Serial.print("Connected to ");
	Serial.println(ssid);
	Serial.print("IP address: ");
	Serial.println(WiFi.localIP().toString());

	if (mdns.begin(HOST_NAME, WiFi.localIP())){
		Serial.println("MDNS responder started");
	}

	server.on("/", handleRoot);
	server.on("/ir", handleIr);
	server.on("/aux", handleAux);
	server.on("/sendraw", handleSendRaw);

	server.on("/jscript.js", [](){ server.send(200, "text/html", ""); });
	server.on("/sensData", []()
						{ server.send(200, "text/html", getSensData().c_str()); });

	server.onNotFound(handleNotFound);

	// Инициализация DS18B20
	DS18B20.begin();

	server.begin();
	Serial.println("HTTP server started");

	delay(5000);
	autostartAC();

	watchDog.start();
}

void loop(void)
{
	server.handleClient();
	watchDog.update();
}
