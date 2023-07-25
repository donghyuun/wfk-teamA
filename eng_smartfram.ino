#include <ESP8266WiFi.h>
#include <SparkFun_RHT03.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <EEPROM.h>
#include <stdio.h>
#include <math.h>

#define MAX_DEGREE 10

const char *ssid = "";     // current WiFi name
const char *password = ""; // current WiFi password
WiFiServer server(80);     // server

// NTP server time
const char *ntpServer = "pool.ntp.org";
uint8_t timeZone = 9;
uint8_t summerTime = 0;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntpServer);

String formettedTime;
int year, month, day, hour, minute, second;

// Pin declaration
int motorPin[] = {D1, D2};
int ledPin = D3;
int submotorPin = D4;
int soilPin[] = {D5, D6};
int rhtPin = D7;

// HIGH is OFF because the relay module is used
int isLedOn = 1;
int motorOn[] = {1, 1};
int isSubmotorOn = 1;

// temperature & humidity declaration
RHT03 rht;
float tempC;
float humidity;

// humidity chart
byte soilHumidity1[24] = {0};
byte soilHumidity2[24] = {0};
/*변경(Change)*/
byte humforpred1[24] = {0};
byte humforpred2[24] = {0};
int cnt = 0;

bool doItJustOnce = false;

// Array for storing Log
String logs[1000];
int logcount = 0;

// Number of elements in the predicted dataset array
byte x[24] = {0};
// Predictive Results Storage Variables
byte prediction1 = 0;
byte prediction2 = 0;

//변경(Change): Generate predictive models and functions
void polynomial_regression(byte x[], byte y[], int n, int degree, byte coef[])
{
    byte X[MAX_DEGREE * 2 + 1] = {0};
    byte Y[MAX_DEGREE + 1] = {0};
    byte B[MAX_DEGREE + 1][MAX_DEGREE + 1] = {0};

    // Construct the X matrix and Y vector
    for (int i = 0; i < n; i++)
    {
        byte temp = 1.0;
        for (int j = 0; j <= degree; j++)
        {
            X[j] += temp;
            temp *= x[i];
        }
        temp = y[i];
        for (int j = 0; j <= degree; j++)
        {
            Y[j] += temp;
            temp *= x[i];
        }
    }

    // Construct the B matrix
    for (int i = 0; i <= degree; i++)
    {
        for (int j = 0; j <= degree; j++)
        {
            B[i][j] = X[i + j];
        }
    }

    // Gaussian elimination
    for (int i = 0; i <= degree; i++)
    {
        byte div = B[i][i];
        for (int j = i; j <= degree; j++)
        {
            B[i][j] /= div;
        }
        Y[i] /= div;
        for (int j = i + 1; j <= degree; j++)
        {
            byte mul = B[j][i];
            for (int k = i; k <= degree; k++)
            {
                B[j][k] -= B[i][k] * mul;
            }
            Y[j] -= Y[i] * mul;
        }
    }

    // Back substitution
    for (int i = degree; i >= 0; i--)
    {
        coef[i] = Y[i];
        for (int j = i + 1; j <= degree; j++)
        {
            coef[i] -= B[i][j] * coef[j];
        }
    }
}

byte predict(byte x, byte coef[], int degree)
{
    byte result = 0;
    byte temp = 0;
    for (int i = 0; i <= degree; i++)
    {
        result += coef[i] * temp;
        temp *= x;
    }
    return result;
}

int soilValue(int pin)
{
    for (int i = 0; i < 2; i++)
    {
        if (i == pin)
        {
            digitalWrite(soilPin[i], HIGH);
        }
        else
        {
            digitalWrite(soilPin[i], LOW);
        }
    }
    return analogRead(A0);
}

void writeLog(String text)
{
    if (logcount > 999)
        logcount = 0; // Return to zero when the log array is full
    String currentTime = String(year) + "-" + String(month) + "-" + String(day) + " " + formettedTime;
    logs[logcount] = currentTime + " | " + text;
    logcount += 1;
}

void setup()
{
    
    // default setting
    writeLog("server turned on");
    pinMode(A0, INPUT);
    pinMode(ledPin, OUTPUT);
    pinMode(submotorPin, OUTPUT);
    for (int i = 0; i < 2; i++)
    {
        pinMode(motorPin[i], OUTPUT);
        pinMode(soilPin[i], OUTPUT);
    }
    rht.begin(rhtPin);
    EEPROM.begin(122); //(Change): 48 -> 96 -> 120 -> 122 //(assignment) //(memory)
    writeLog("Pin setting and RHT, EEPROM start - complete");

    // Get Humidity Values
    for (int i = 0; i < EEPROM.length(); i++)
    {
        if (EEPROM.read(i) > 100)
            continue; // The initial value of the initialized EEPROM was 255.
        if (i < 24)
        {
            soilHumidity1[i] = EEPROM.read(i);
        }
        else
        {
            soilHumidity2[i - 24] = EEPROM.read(i);
        }
    }
    writeLog("Completed loading humidity values stored in EEPROM");

    // Turn off the whole thing
    digitalWrite(ledPin, isLedOn);           // Turn on and off the LED
    digitalWrite(submotorPin, isSubmotorOn); // Turn on and off the submotor
    for (int i = 0; i < 2; i++)
    {
        digitalWrite(motorPin[i], motorOn[i]);
    }

    Serial.begin(115200); // Serial communication, speed 115200
    delay(10);

    // Wi-Fi connection
    writeLog("Connecting to Wi-Fi");
    WiFi.mode(WIFI_STA);

    writeLog("Connecting to Wi-Fi " + String(ssid)); // modifying in translation

    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(100);
        Serial.print(".");
    }

    writeLog("Wi-Fi connection complete");

    // server start
    server.begin();
    writeLog("server started");

    // recall server time
    timeClient.begin();
    timeClient.setTimeOffset(3600 * timeZone);
    timeClient.update();
    formettedTime = timeClient.getFormattedTime();
    hour = timeClient.getHours();
    minute = timeClient.getMinutes();
    second = timeClient.getSeconds();
    writeLog("NTP Time Recall Complete");
}

void loop()
{
    delay(50);
    WiFiClient client = server.available();
    timeClient.update(); // update time

    time_t epochTime = timeClient.getEpochTime();
    struct tm *ptm = gmtime((time_t *)&epochTime);

    formettedTime = timeClient.getFormattedTime();
    hour = timeClient.getHours();
    minute = timeClient.getMinutes();
    second = timeClient.getSeconds();

    year = ptm->tm_year + 1900; // It adds 1900 to match the current year.
    month = ptm->tm_mon + 1;
    day = ptm->tm_mday;

    // request GET
    String req = client.readStringUntil('\r');
    client.flush();

    if (req.indexOf("led/on") != -1)
    {
        isLedOn = 0;
        writeLog("LED on by user");
    }
    else if (req.indexOf("led/off") != -1)
    {
        isLedOn = 1;
        writeLog("LED off by user");
    }
    else if (req.indexOf("motor/sub/on") != -1)
    {
        isSubmotorOn = 0;
        writeLog("Submotor on by user");
    }
    else if (req.indexOf("motor/sub/off") != -1)
    {
        isSubmotorOn = 1;
        writeLog("Submotor off by user");
    }

    digitalWrite(ledPin, isLedOn);           // Turn on and off the LED
    digitalWrite(submotorPin, isSubmotorOn); // Turn on and off the submotor

    int soilValues[2] = {0};   // Soil humidity data source
    int soilPercents[2] = {0}; // values processed in percentage

    for (int i = 0; i < 2; i++)
    {
        soilValues[i] = soilValue(i);
        delay(100);
    }

    for (int i = 0; i < 2; i++)
    {
        soilPercents[i] = map(soilValues[i], 1024, 0, 0, 100);
    }

    if (timeClient.getMinutes() == 1 && !doItJustOnce)
    { // Run only once on time (run every minute to meet the run time difference)
        if (!hour)
        { // on time
            // initialize EEPROM
            for (int i = 0; i < EEPROM.length(); i++)
            {
                EEPROM.write(i, 0);
            }
            writeLog("EEPROM initialized");

            // initialize humidity
            for (int i = 0; i < 24; i++)
            {
                soilHumidity1[i] = 0;
                soilHumidity2[i] = 0;
            }
            writeLog("Soil humidity value initialized");
        }

        if (hour >= 6 && hour <= 19)
        { // Turned on during the day
            if (isLedOn)
            {
                writeLog("LED on for time");
                isLedOn = 0;
            }
        }
        else
        {
            if (!isLedOn)
            { // Turned off at night
                writeLog("LED off for time");
                isLedOn = 1;
            }
        }

        // Store humidity in EEPROM
        // 0 ~ 23 is Humidity 1
        EEPROM.write(hour, soilPercents[0]);
        soilHumidity1[hour] = soilPercents[0];
        // 24 ~ 47 is Humidity 2
        EEPROM.write(hour + 24, soilPercents[1]);
        soilHumidity2[hour] = soilPercents[1];
        EEPROM.commit();
        doItJustOnce = true;

        writeLog("Humidity 1 " + String(soilPercents[0]) + " EEPROM record");
        writeLog("Humidity 2 " + String(soilPercents[1]) + " EEPROM record");
    }
    else if (timeClient.getMinutes() == 2 && doItJustOnce)
    { // Change the value so that it can run again on time
        doItJustOnce = false;
    }

    // Checking the temperature and humidity
    int updateRht = rht.update(); // Returns 1 when temperature and humidity are called via RHT

    /* Additional: Save to the list of moisture measurements every 15 minutes*/ //(Change)
    if (timeClient.getMinutes() % 15 == 0)
    {
        if (cnt < 24)
        {
            humforpred1[cnt] = soilPercents[0];
            humforpred2[cnt] = soilPercents[1];
            cnt++;
        }
        else
        {
            for (int i = 0; i < 23; i++)
            {
                humforpred1[i] = humforpred1[i + 1];
                humforpred2[i] = humforpred2[i + 1];
            }
            humforpred1[23] = soilPercents[0];
            humforpred2[23] = soilPercents[1];
        }

        for (int i = 0; i < cnt; i++)
        {
            x[i] = 15 * (i + 1);
        }

        int degree = 2;

        //************1. Prediction for pot 1************ //(Change)
        byte coef1[MAX_DEGREE + 1];

        polynomial_regression(x, humforpred1, cnt, degree, coef);

        printf("Coefficients of the polynomial:\n");
        for (int i = degree; i >= 0; i--)
        {
            printf("x%d: %d\n", i, coef[i]);
        }

        // prediction test
        byte test_x1 = 6;
        prediction1 = predict(test_x1, coef, degree);
        printf("Pot 1 Prediction at x = %d: %d\n", test_x1, prediction1); // we can put the current time +1 in x

        //************2. Prediction for pot 2************ //(Change)
        byte coef2[MAX_DEGREE + 1];

        polynomial_regression(x, humforpred2, cnt, degree, coef);

        printf("Coefficients of the polynomial:\n");
        for (int i = degree; i >= 0; i--)
        {
            printf("x%d: %d\n", i, coef[i]);
        }

        // prediction test
        byte test_x2 = 6;
        prediction2 = predict(test_x2, coef, degree);
        printf("Pot 2 Prediction2 at x = %d: %d\n", test_x2, prediction2); // we can put the current time +1 in x

        prediction[2] = [ predicition1, prediction2 ];
        for (int i = 0; i < 2; i++)
        {
            if (predicition[i] > 50)
            { // Comparison of predicted and thresholds - threshold changes required after validation
                writeLog("No." + String(i + 1) + " motor turned on due to watering levels");
                motorOn[i] = 0; // motor turned on
            }
            else
            {
                if (!motorOn[i])
                    writeLog("No." + String(i + 1) + " motor turned off due to watering levels");
                motorOn[i] = 1; // motor turned off
            }
            digitalWrite(motorPin[i], motorOn[i]);
        }
    }

    if (updateRht == 1)
    {
        humidity = rht.humidity();
        tempC = rht.tempC();
        writeLog("Temperature and humidity updated  Temp: " + String(tempC) + "°C, Hum: " + String(humidity) + "%");
    }

    /*
    ====== HTML declaration ======
    */
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println("Connection: close");
    client.println();
    client.println("<!DOCTYPE html>");
    client.println("<html xmlns='http://www.w3.org/1999/xhtml'>");
    client.println("<head>\n<meta http-equiv='Content-Type' content='text/html; charset=utf-8' />");
    client.println("<script src=\"https://cdn.tailwindcss.com\"></script>");
    client.println("<title>Smart Farm</title>"); //  Setting the Web Server Page Title
    client.println("</head>");

    // body tag declaration
    client.println("<body>");
    client.println("<div class=\"text-8xl p-8 font-bold\"><a href=\"/\">Yoo Farm</a></div>");
    client.println("<div class=\"border-4 border-gray-800 rounded m-2 p-2\">");
    client.println("<div class=\"flex flex-row mb-6\">");
    client.println("<div class=\"basis-1/2 relative mb-6\">");

    // LED
    client.print("<span class=\"text-2xl\">LED: ");
    isLedOn
        ? client.println("<span class=\"text-2xl font-bold\">OFF</span>")
        : client.println("<span class=\"text-2xl font-bold text-green-400\">ON</span>");
    client.println("</span>");
    client.println("<div class=\"flex\">\
                    <div class=\"inline-flex shadow-md hover:shadow-lg focus:shadow-lg\" role=\"group\">\
                        <button type=\"button\" class=\"rounded-l inline-block px-6 py-2.5 bg-blue-600 text-white font-medium text-xs leading-tight uppercase hover:bg-blue-700 focus:bg-blue-700 focus:outline-none focus:ring-0 active:bg-blue-800 transition duration-150 ease-in-out\" onclick=\"location.href='/led/on'\">On</button>\
                        <button type=\"button\" class=\" rounded-r inline-block px-6 py-2.5 bg-blue-600 text-white font-medium text-xs leading-tight uppercase hover:bg-blue-700 focus:bg-blue-700 focus:outline-none focus:ring-0 active:bg-blue-800 transition duration-150 ease-in-out\" onclick=\"location.href='/led/off'\">Off</button></div></div>"); // LED on & off button
    client.println("</div>");                                                                                                                                                                                                                                                                                                                                // <div class="basis-1/2 relative mb-6">

    client.println("<div class=\"basis-1/2 relative\">");

    // submotor
    client.print("<span class=\"text-2xl\">Submotor: ");
    isSubmotorOn
        ? client.println("<span class=\"text-2xl font-bold\">OFF</span>")
        : client.println("<span class=\"text-2xl font-bold text-green-400\">ON</span>");
    client.println("</span>");
    client.println("<div class=\"flex\">\
                    <div class=\"inline-flex shadow-md hover:shadow-lg focus:shadow-lg\" role=\"group\">\
                        <button type=\"button\" class=\"rounded-l inline-block px-6 py-2.5 bg-blue-600 text-white font-medium text-xs leading-tight uppercase hover:bg-blue-700 focus:bg-blue-700 focus:outline-none focus:ring-0 active:bg-blue-800 transition duration-150 ease-in-out\" onclick=\"location.href='/motor/sub/on'\">On</button>\
                        <button type=\"button\" class=\" rounded-r inline-block px-6 py-2.5 bg-blue-600 text-white font-medium text-xs leading-tight uppercase hover:bg-blue-700 focus:bg-blue-700 focus:outline-none focus:ring-0 active:bg-blue-800 transition duration-150 ease-in-out\" onclick=\"location.href='/motor/sub/off'\">Off</button></div></div>");
    client.println("</div>"); // <div class="basis-1/2 relative">

    // Temperature and humidity
    client.println("<div class=\"basis-1/4\">");
    client.println("<div><span class=\"text-2xl\">Temp: </span>");
    client.print("<span class=\"text-2xl font-bold\">");
    client.print(tempC); // temperature
    client.println("<span class=\"text-red-600\">°C</span></span>");
    client.println("</div>");
    client.println("<div><span class=\"text-2xl\">Hum: </span>");
    client.print("<span class=\"text-2xl font-bold\">");
    client.print(humidity); // humidity
    client.println("<span>%</span></span>");
    client.println("</div>");

    client.println("</div>"); // <div class="basis-1/4">
    client.println("</div>"); // <div class="flex flex-row mb-6">

    // Pot card declaration
    client.println("<div class=\"h-fit grid grid-cols-2 gap-4 text-center\">");
    client.println("<div class=\"border-2 border-violet-400 rounded\">");
    client.println("<span class=\"mb-1 text-2xl font-bold\">Pot 1</span><hr/>");
    client.print("<div class=\"mb-3 mt-3 text-3xl font-bold\">Motor ");
    motorOn[0]
        ? client.println("<span class=\"text-3xl font-bold\">OFF</span>")
        : client.println("<span class=\"text-3xl font-bold text-green-400\">ON</span>");
    client.println("</div>"); // <div class="mb-3 mt-3 text-3xl font-bold">

    client.println("<div class=\"mb-1 text-lg font-bold\">Soil Humidity</div>");
    client.println("<div class=\"mx-auto w-9/12 h-6 bg-gray-200 rounded-full dark:bg-gray-700\">");
    client.println("<div class=\"h-6 bg-gradient-to-r from-cyan-500 to-indigo-500 rounded-full font-bold text-slate-200\" style=\"width:");
    client.print(soilPercents[0]);
    client.print("%\">");
    client.print(soilPercents[0]);
    client.println("%</div></div>");
    client.print("<div class=\"p-5 shadow-lg rounded-lg overflow-hidden\">\
                    <div class=\"py-3 px-5 bg-gray-50 font-bold\">");
    client.print(String(year) + "-" + String(month) + "-" + String(day));
    client.println(" Pot 1 Soil Hum</div>\
                    <canvas class=\"p-1\" id=\"chartLine1\"></canvas>\
                </div>");
    client.println("</div>");

    client.println("<div class=\"border-2 border-violet-400 rounded\">");
    client.println("<span class=\"mb-1 text-2xl font-bold\">Pot 2</span><hr/>");
    client.print("<div class=\"mb-3 mt-3 text-3xl font-bold\">Motor ");
    motorOn[1]
        ? client.println("<span class=\"text-3xl font-bold\">OFF</span>")
        : client.println("<span class=\"text-3xl font-bold text-green-400\">ON</span>");
    client.println("</div>");

    client.println("<div class=\"mb-1 text-lg font-bold\">Soil Humidity</div>");
    client.println("<div class=\"mx-auto w-9/12 h-6 bg-gray-200 rounded-full dark:bg-gray-700\">");
    client.println("<div class=\"h-6 bg-gradient-to-r from-cyan-500 to-indigo-500 rounded-full font-bold text-slate-200\" style=\"width:");
    client.print(soilPercents[1]);
    client.print("%\">");
    client.print(soilPercents[1]);
    client.println("%</div></div>");

    // Charts
    client.print("<div class=\"p-5 shadow-lg rounded-lg overflow-hidden\">\
                    <div class=\"py-3 px-5 bg-gray-50 font-bold\">");
    client.print(String(year) + "-" + String(month) + "-" + String(day));
    client.println(" Pot 1 Soil Hum</div>\
                    <canvas class=\"p-1\" id=\"chartLine2\"></canvas>\
                </div>");

    client.println("</div></div></div>");

    // Log window
    client.println("<div class=\"m-2 mb-0 p-2 pl-4 rounded-t-lg bg-slate-400\">\
        <span class=\"text-4xl font-bold\">Log</span>\
    </div>");
    client.println("<div class=\"m-2 mt-0 p-2 pl-4 h-full bg-slate-600 font-bold text-white overflow-scroll\" style=\"height: 30vh;\">");
    for (int i = logcount; i >= 0; i--)
    {
        client.print("<div>");
        client.print(logs[i]);
        client.println("</div>");
    }
    client.println("</div>");
    client.println("</body>");

    // Line Charts
    client.println("<script src=\"https://cdn.jsdelivr.net/npm/chart.js\"></script>");
    client.println("<script>");

    client.print("const humidity = ");
    client.print("[[");
    for (int i = 0; i < 24; i++)
    {
        client.print(soilHumidity1[i]);
        client.print(",");
    }
    client.print("],[");
    for (int i = 0; i < 24; i++)
    {
        client.print(soilHumidity2[i]);
        client.print(",");
    }
    client.println("]]");

    client.println("const labels = [\"0시\", \"1시\", \"2시\", \"3시\", \"4시\", \"5시\", \"6시\", \"7시\", \"8시\", \"9시\", \"10시\", \"11시\", \"12시\", \"13시\", \"14시\", \"15시\", \"16시\", \"17시\", \"18시\", \"19시\", \"20시\", \"21시\", \"22시\", \"23시\"]");
    client.println("const data = [{");
    client.println("labels: labels,");
    client.println("datasets: [{");
    client.println("label: \"Soil Humidity\",");
    client.println("backgroundColor: \"hsl(252, 82.9%, 67.8%)\",");
    client.println("borderColor: \"hsl(252, 82.9%, 67.8%)\",");
    client.println("data: humidity[0],},");
    client.println("],},{");
    client.println("labels: labels,");
    client.println("datasets: [{");
    client.println("label: \"Soil Humidity\",");
    client.println("backgroundColor: \"hsl(252, 82.9%, 67.8%)\",");
    client.println("borderColor: \"hsl(252, 82.9%, 67.8%)\",");
    client.println("data: humidity[1],},],}]");
    client.println("var chartLine = new Chart(");
    client.println("document.getElementById(\"chartLine1\"),");
    client.println("{type: \"line\",");
    client.println("data:data[0],");
    client.println("options: {},})");
    client.println("var chartLine2 = new Chart(");
    client.println("document.getElementById(\"chartLine2\"),");
    client.println("{type: \"line\",");
    client.println("data:data[1],");
    client.println("options: {},})");

    client.println("</script>");

    // close html
    client.println("</html>");
}