#include <ESP8266WiFi.h>
#include <SparkFun_RHT03.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <EEPROM.h>

//****날씨***//
#include <WiFiClient.h>
#include <ArduinoJson.h>

//인공지능 함수 구조체
typedef struct TreeNode {
    int value;
    struct TreeNode* left;
    struct TreeNode* right;
} TreeNode;




//***최대 차수***
#define MAX_DEGREE 10

const char* ssid = "AndroidHotspot4250"; // 사용 중 인 와이파이 이름
const char* password = "42504250"; // 와이파이 패스워드
WiFiServer server(80); // 서버

// NTP 서버시간
const char* ntpServer = "pool.ntp.org";
uint8_t timeZone = 9;
uint8_t summerTime = 0;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntpServer);

//날씨
// OpenWeatherMap API 서버 정보
const char* weatherServer = "api.openweathermap.org";
const int weatherPort = 80;
const char* apiKey = "a4e873808077c72854f9549953b758af";
const char* city = "1643084"; // 인도네시아의 도시명과 국가 코드

String formettedTime;
int year, month, day, hour, minute, second;


// Pin 선언
int motorPin[] = { 5, 4 };
int ledPin = 0;
int submotorPin = 2;
int soilPin[] = { 14, 12 };
int rhtPin = 13;

// 릴레이모듈을 사용하므로 HIGH가 꺼진상태
int isLedOn = 1;
int motorOn[] = { 1, 1 };
int isSubmotorOn = 1;

// 온습도 선언
RHT03 rht;
float tempC;
float humidity;

// 수분차트
byte soilHumidity1[24] = { 0 };
byte soilHumidity2[24] = { 0 };
// ***예측 위한 데이터 스택***
int humForPred1[24] = { 0 };
int humForPred2[24] = { 0 };
int cnt = 0;
// ***humForPred의 index역할***
int x[24] = { 0 };
// ***예측 결과 저장 변수***
int prediction1 = 0;
int prediction2 = 0;

// ***인도네시아 현재 날씨 저장 변수들*** //
float zaka_temperature;
float zaka_humidity;
float zaka_windSpeed;

bool doItJustOnce = false;

// Log를 저장하기위한 배열
String logs[1000];
int logcount = 0;

int soilValue(int pin) {
    for (int i = 0; i < 2; i++) {
        if (i == pin) {
            digitalWrite(soilPin[i], HIGH);
        }
        else {
            digitalWrite(soilPin[i], LOW);
        }
    }
    return analogRead(A0);
}

void writeLog(String text) {
    if (logcount > 999) logcount = 0; // 로그 배열이 다 차면 다시 0으로 돌아감
    String currentTime = String(year) + "-" + String(month) + "-" + String(day) + " " + formettedTime;
    logs[logcount] = currentTime + " | " + text;
    Serial.print(currentTime);
    Serial.print(" | ");
    Serial.println(text);
    logcount += 1;
}

//***예측 모델 및 예측 함수 생성***//
//index => 배열 x, value => 배열 y
//n => data 갯수, degree => 정해진 다항식 차수, coef => 다항식 계수 담을 배열 
//polynomial_regression(x, humForPred1, cnt, degree, coef1);//coef1 설정
TreeNode* createDecisionTree(int y[], int start, int end) {
    // 데이터의 수가 1개인 경우 단일 노드를 생성하여 반환
    if (start == end) {
        TreeNode* node = (TreeNode*)malloc(sizeof(TreeNode));
        node->value = y[start];
        node->left = NULL;
        node->right = NULL;
        return node;
    }

    // 데이터의 중간 인덱스를 구함
    int mid = (start + end) / 2;

    // 중간 값으로 의사결정 트리 노드 생성
    TreeNode* node = (TreeNode*)malloc(sizeof(TreeNode));
    node->value = y[mid];

    // 중간 인덱스를 기준으로 왼쪽과 오른쪽 자식 노드 재귀적으로 생성
    node->left = createDecisionTree(y, start, mid);
    node->right = createDecisionTree(y, mid + 1, end);

    return node;
}




//***테스트 케이스 x 에 대한 예측값을 반환***
//테스트 케이스 x, 정해진 coef, 정해진 차수 degree
int predict(TreeNode* root, int x) {
    // 루트 노드부터 시작하여 x 값에 해당하는 예측값을 찾음
    TreeNode* current = root;
    while (current->left != NULL || current->right != NULL) {
        if (x < current->value) {
            current = current->left;
        }
        else {
            current = current->right;
        }
    }
    // 찾은 예측값 반환
    return current->value;
}
// 추가된 함수: 가장 가까운 값을 찾는다.
int findClosestValue(TreeNode* root, int x, int cnt, int* x_values) {
    int closest_value = 9999;
    TreeNode* closest_node = NULL;
    for (int i = 0; i < cnt; i++) {
        if (abs(x - x_values[i]) < abs(x - closest_value)) {
            closest_value = x_values[i];
            closest_node = root; // 현재 가장 가까운 노드를 저장
        }
    }
    return closest_node->value;
}


//********날씨***********//
void getWeather() {
    // Wi-Fi 클라이언트 객체 생성
    WiFiClient client;

    // OpenWeatherMap API에 요청 보내기
    if (client.connect(weatherServer, 80)) {
        client.print("GET /data/2.5/weather?id=");
        client.print(city);
        client.print("&appid=");
        client.print(apiKey);
        client.println(" HTTP/1.1");
        client.print("Host: ");
        client.println(weatherServer);
        client.println("Connection: close");
        client.println();

        // 응답 수신 및 처리
        while (client.connected()) {
            if (client.available()) {
                String line = client.readStringUntil('\n');
                if (line == "\r") {
                    break;
                }
            }
        }

        // JSON 파싱 및 날씨 정보 출력
        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, client);
        if (error) {
            Serial.println("Error parsing JSON");
            return;
        }

        const char* description = doc["weather"][0]["description"];
        zaka_temperature = doc["main"]["temp"];
        zaka_humidity = doc["main"]["humidity"];
        zaka_windSpeed = doc["wind"]["speed"];

        Serial.println("Current Weather in Jakarta, Indonesia:");
        Serial.print("Description: ");
        Serial.println(description);
        Serial.print("Temperature: ");
        Serial.print(zaka_temperature);
        Serial.println(" °C");
        Serial.print("Humidity: ");
        Serial.print(zaka_humidity);
        Serial.println(" %");
        Serial.print("Wind Speed: ");
        Serial.print(zaka_windSpeed);
        Serial.println(" m/s");

    }
    else {
        Serial.println("Failed to connect to OpenWeatherMap API");
    }
    client.stop();
}



void setup() {
    // 기본 설정
    writeLog("서버 켜짐");
    pinMode(A0, INPUT);
    pinMode(ledPin, OUTPUT);
    pinMode(submotorPin, OUTPUT);
    for (int i = 0; i < 2; i++) {
        pinMode(motorPin[i], OUTPUT);
        pinMode(soilPin[i], OUTPUT);
    }
    rht.begin(rhtPin);
    EEPROM.begin(121);//48 + 48 + 1(cnt) + 24

    writeLog("핀 설정 및 RHT, EEPROM 시작 완료");

    // 습도 값 불러오기
    for (int i = 0; i < EEPROM.length(); i++) {
        if (EEPROM.read(i) > 100) continue; // 초기화 된 EEPROM의 초기값이 255였음.
        if (i < 24) {
            soilHumidity1[i] = EEPROM.read(i);
        }
        else {
            soilHumidity2[i - 24] = EEPROM.read(i);
        }
    }
    writeLog("EEPROM에 저장되어있는 습도 값 불러오기 완료");


    // 전체적으로 꺼주기
    digitalWrite(ledPin, isLedOn); // LED 켜고 끔
    digitalWrite(submotorPin, isSubmotorOn); // 물버림 모터 켜고 끔
    for (int i = 0; i < 2; i++) {
        digitalWrite(motorPin[i], motorOn[i]);
    }


    Serial.begin(115200); // 시리얼 통신, 속도 115200
    delay(10);

    // 와이파이 연결
    writeLog("와이파이 연결 중");
    WiFi.mode(WIFI_STA);

    writeLog("와이파이" + String(ssid) + "에 연결");

    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(100);
        Serial.print("!");
    }

    writeLog("와이파이 연결 완료");
    Serial.println("와이파이 연결 완료");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // 서버 시작
    server.begin();
    writeLog("서버 시작 됨");

    // server시간 가져오기
    timeClient.begin();
    timeClient.setTimeOffset(3600 * timeZone);
    timeClient.update();
    formettedTime = timeClient.getFormattedTime();
    hour = timeClient.getHours();
    minute = timeClient.getMinutes();
    second = timeClient.getSeconds();
    writeLog("NTP 시간 불러오기 완료");
}

void loop() {
    // put your main code here, to run repeatedly:
    delay(50);
    WiFiClient client = server.available();

    timeClient.update(); // 시간 업데이트

    time_t epochTime = timeClient.getEpochTime();
    struct tm* ptm = gmtime((time_t*)&epochTime);

    formettedTime = timeClient.getFormattedTime();
    hour = timeClient.getHours();
    minute = timeClient.getMinutes();
    second = timeClient.getSeconds();

    year = ptm->tm_year + 1900; // 현재 년도와 맞춰주기 위해 1900을 더해준다.
    month = ptm->tm_mon + 1;
    day = ptm->tm_mday;

    // GET 요청
    String req = client.readStringUntil('\r');
    Serial.println(client.readStringUntil('\r'));
    client.flush();

    if (req.indexOf("led/on") != -1) {
        isLedOn = 0;
        writeLog("사용자로 인해 led 켜짐");
    }
    else if (req.indexOf("led/off") != -1) {
        isLedOn = 1;
        writeLog("사용자로 인해 led 꺼짐");
    }
    else if (req.indexOf("motor/sub/on") != -1) {
        //isSubmotorOn = 0;
        //writeLog("사용자로 인해 물빼기 모터 켜짐");
    }
    else if (req.indexOf("motor/sub/off") != -1) {
        //isSubmotorOn = 1;
        //writeLog("사용자로 인해 물빼기 모터 꺼짐");
    }



    digitalWrite(ledPin, isLedOn); // LED 켜고 끔
    //digitalWrite(submotorPin, isSubmotorOn); // 물버림 모터 켜고 끔
    digitalWrite(submotorPin, 1); // 물버림 모터 켜고 끔

    //*******물빼기 모터 디지털 핀 상태 확인***********//
    int pinState = digitalRead(2); // 디지털 핀 7의 상태를 읽어옴

    // 디지털 핀 7의 상태에 따라서 시리얼 모니터에 메시지 출력
    if (pinState == HIGH) {
        Serial.println("디지털 핀 2은 HIGH 상태입니다.");
    }
    else {
        Serial.println("디지털 핀 2은 LOW 상태입니다.");
    }
    //**********************************************//

    int soilValues[2] = { 0 };
    int soilPercents[2] = { 0 };

    for (int i = 0; i < 2; i++) {
        soilValues[i] = soilValue(i);
        delay(100);
    }

    for (int i = 0; i < 2; i++) {
        if (soilValues[i] > 500) { // 물주는값
            writeLog("물주는값으로 인해 " + String(i + 1) + "번 모터 켜짐");
            motorOn[i] = 0; // 모터켜짐
        }
        else {
            if (!motorOn[i]) writeLog("물주는값으로 인해 " + String(i + 1) + "번 모터 꺼짐");
            motorOn[i] = 1; // 모터꺼짐
        }
        digitalWrite(motorPin[i], motorOn[i]);
    }

    for (int i = 0; i < 2; i++) {
        soilPercents[i] = map(soilValues[i], 1024, 0, 0, 100);
    }

    if (timeClient.getMinutes() == 15 && !doItJustOnce) { // 정시에 한번만 실행 (실행시차를 맞추기위해 1분에 실행 함)
        if (!hour) { // 정각이라면(0(24)시 00분일때)
            // EEPROM 초기화
            for (int i = 0; i < EEPROM.length(); i++) {
                EEPROM.write(i, 0);
            }
            writeLog("EEPROM 초기화");

            // 수분값 초기화
            for (int i = 0; i < 24; i++) {
                soilHumidity1[i] = 0;
                soilHumidity2[i] = 0;
            }
            writeLog("토양 수분값 초기화");
        }

        if (hour >= 6 && hour <= 19) { // 낮시간동안 켜짐
            if (isLedOn) {
                writeLog("시간 상 led 켜짐");
                isLedOn = 0;
            }
        }
        else {
            if (!isLedOn) { // 밤에는 꺼짐
                writeLog("시간 상 led 꺼짐");
                isLedOn = 1;
            }
        }

        // 습도 EEPROM에 저장
        // 0 ~ 23은 1번 수분
        EEPROM.write(hour, soilPercents[0]);
        soilHumidity1[hour] = soilPercents[0];
        // 24 ~ 47은 2번 수분
        EEPROM.write(hour + 24, soilPercents[1]);
        soilHumidity2[hour] = soilPercents[1];
        EEPROM.commit();
        doItJustOnce = true;

        writeLog("1번 수분 " + String(soilPercents[0]) + " EEPROM 기록");
        writeLog("2번 수분 " + String(soilPercents[1]) + " EEPROM 기록");
    }
    else if (timeClient.getMinutes() == 2 && doItJustOnce) { // 다음 정시에 다시 실행될 수 있게 값을 바꿔 줌
        doItJustOnce = false;
    }

    /*******15분(테스트: 1분)마다 수분 측정값 list에 저장하기*******/
    if (timeClient.getMinutes() % 1 == 0) {
        if (cnt < 24) {
            humForPred1[cnt] = soilPercents[0];
            humForPred2[cnt] = soilPercents[1];
            cnt++;//현재 배열안에 들어있는 원소의 개수
        }
        else {
            for (int i = 0; i < 23; i++) {//스택 구조 => 앞으로 한칸씩 당기고, 뒤에 삽입
                humForPred1[i] = humForPred1[i + 1];
                humForPred2[i] = humForPred2[i + 1];
            }
            humForPred1[23] = soilPercents[0];
            humForPred2[23] = soilPercents[1];
        }
        Serial.println(cnt);//테스트용

        for (int i = 0; i < cnt; i++) {//공통된 x값, 즉 y값(15분마다의 화분별 수분 배열)의 index 역할
            x[i] = 1 * (i + 1);//15 -> 1
        }

        int degree = 3;//예측 모델 식의 차수

        //*********화분 1에 대한 예측*********//
        int coef1[MAX_DEGREE + 1] = { 0 };//예측값 담기위함
        for (int i = 0; i < cnt; i++) {
            writeLog("humForPred1[" + String(i) + "] : " + String(humForPred1[i]));
            writeLog("humForPred2[" + String(i) + "] : " + String(humForPred2[i]));
        }


        //예측 테스트
        int test_x1 = cnt+1;
        // 의사결정 트리 생성
        TreeNode* root1 = createDecisionTree(humForPred1, 0, cnt - 1);
        //예측값 구하기
        int closest_value1 = findClosestValue(root1, test_x1, cnt, x);
        printf("Closest value to x = %d: %d\n", test_x1, closest_value1);

        writeLog("화분 1 Prediction at x = " + String(test_x1) + ": " + closest_value1); //이때 x를 현재 시각+1이나 해서 넣으면 될듯듯


        // //*********화분 2에 대한 예측*********//

        //예측 테스트
        int test_x2 = cnt+1;
        // 의사결정 트리 생성
        TreeNode* root2 = createDecisionTree(humForPred2, 0, cnt - 1);
        //예측값 구하기
   
        int closest_value2 = findClosestValue(root2, test_x2, cnt, x);
        printf("Closest value to x = %d: %d\n", test_x2, closest_value2);

        writeLog("화분 2 Prediction at x = " + String(test_x2) + ": " + closest_value2); //이때 x를 현재 시각+1이나 해서 넣으면 될듯듯


        //*********예측값을 이용한 모턴 움직임 동작*********//
        int prediction[2] = { prediction1, prediction2 };
        for (int i = 0; i < 2; i++) {
            if (prediction[i] < 40) {
                writeLog("물주는값으로 인해 " + String(i + 1) + "번 모터 켜짐");
            }
            else {
                if (!motorOn[i]) writeLog("물주는값으로 인해 " + String(i + 1) + "번 모터 꺼짐");
                motorOn[i] = 1; // 모터꺼짐
            }
            digitalWrite(motorPin[i], motorOn[i]);
        }
    }
    //*******************************************//


    // 온습도 확인하기
    int updateRht = rht.update(); // RHT를 통해 온, 습도가 불러진 경우 1을 반환 함

    int degree = 2;
    //**************************************/
    if (updateRht == 1) {
        humidity = rht.humidity();
        tempC = rht.tempC();
        writeLog("온습도 업데이트 됨 온도: " + String(tempC) + "°C, 습도: " + String(humidity) + "%");

        // //테스트용으로 추가함

        //  for(int i = 0; i < EEPROM.length(); i++){
        //         EEPROM.write(i, 0);
        //     }
        //     writeLog("EEPROM 초기화");

        //     // 수분값 초기화
        //     for(int i = 0; i < 24; i++){
        //         soilHumidity1[i] = 0;
        //         soilHumidity2[i] = 0;
        //     }
        //     writeLog("토양 수분값 초기화");


        // 매시간 습도 EEPROM에 저장, but 그냥 delay(50) 마다 계속 실행되는 듯
        // 0 ~ 23은 1번 수분
        EEPROM.write(hour, soilPercents[0]);
        soilHumidity1[hour] = soilPercents[0];
        // 24 ~ 47은 2번 수분
        EEPROM.write(hour + 24, soilPercents[1]);
        soilHumidity2[hour] = soilPercents[1];
        EEPROM.commit();
        doItJustOnce = true;

        writeLog("1번 현재 수분 hour: " + String(hour) + ", " + String(soilPercents[0]) + " EEPROM 기록");
        writeLog("2번 현재 수분 hour: " + String(hour) + ", " + String(soilPercents[1]) + " EEPROM 기록");

        // for(int i = 0; i < sizeof(soilHumidity1)/sizeof(byte); i++){
        //   if(soilHumidity1[i] != 0 && soilHumidity2[i] != 0)
        //   {
        //     writeLog("1번 화분의 " + String(i) + "시의 수분: " + soilHumidity1[i]);
        //     writeLog("2번 화분의 " + String(i) + "시의 수분: " + soilHumidity2[i]);
        //   }
        // }
    }

    //******날씨******//
    getWeather();


    /*
    ====== HTML 선언부 ======
    */
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println("Connection: close");
    client.println();
    client.println("<!DOCTYPE html>");
    client.println("<html xmlns='http://www.w3.org/1999/xhtml'>");
    client.println("<head>\n<meta http-equiv='Content-Type' content='text/html; charset=utf-8' />");
    client.println("<script src=\"https://cdn.tailwindcss.com\"></script>");
    client.println("<title>Smart Farm</title>"); // 웹 서버 페이지 제목 설정
    client.println("</head>");

    // body 태그 선언부
    client.println("<body>");
    client.println("<div class=\"text-8xl p-8 font-bold\"><a href=\"/\">TeamA Farm</a></div>");
    client.println("<div class=\"border-4 border-gray-800 rounded m-2 p-2\">");
    client.println("<div class=\"flex flex-row mb-6\">");
    client.println("<div class=\"basis-1/2 relative mb-6\">");

    // LED >> 수정했음! 반영 바람
    client.print("<span class=\"text-2xl\">LED: ");
    !isLedOn
        ? client.println("<span class=\"text-2xl font-bold\">OFF</span>")
        : client.println("<span class=\"text-2xl font-bold text-green-400\">ON</span>");
    client.println("</span>");
    client.println("<div class=\"flex\">\
                    <div class=\"inline-flex shadow-md hover:shadow-lg focus:shadow-lg\" role=\"group\">\
                        <button type=\"button\" class=\"rounded-l inline-block px-6 py-2.5 bg-blue-600 text-white font-medium text-xs leading-tight uppercase hover:bg-blue-700 focus:bg-blue-700 focus:outline-none focus:ring-0 active:bg-blue-800 transition duration-150 ease-in-out\" onclick=\"location.href='/led/off'\">On</button>\
                        <button type=\"button\" class=\" rounded-r inline-block px-6 py-2.5 bg-blue-600 text-white font-medium text-xs leading-tight uppercase hover:bg-blue-700 focus:bg-blue-700 focus:outline-none focus:ring-0 active:bg-blue-800 transition duration-150 ease-in-out\" onclick=\"location.href='/led/on'\">Off</button></div></div>"); // LED 끄고켜기 버튼
    client.println("</div>"); // <div class="basis-1/2 relative mb-6">


    client.println("<div class=\"basis-1/2 relative\">");

    // 물빼기 모터
    client.print("<span class=\"text-2xl\">물빼기 모터: ");
    isSubmotorOn
        ? client.println("<span class=\"text-2xl font-bold\">OFF</span>")
        : client.println("<span class=\"text-2xl font-bold text-green-400\">ON</span>");
    client.println("</span>");
    client.println("<div class=\"flex\">\
                    <div class=\"inline-flex shadow-md hover:shadow-lg focus:shadow-lg\" role=\"group\">\
                        <button type=\"button\" class=\"rounded-l inline-block px-6 py-2.5 bg-blue-600 text-white font-medium text-xs leading-tight uppercase hover:bg-blue-700 focus:bg-blue-700 focus:outline-none focus:ring-0 active:bg-blue-800 transition duration-150 ease-in-out\" onclick=\"location.href='/motor/sub/on'\">On</button>\
                        <button type=\"button\" class=\" rounded-r inline-block px-6 py-2.5 bg-blue-600 text-white font-medium text-xs leading-tight uppercase hover:bg-blue-700 focus:bg-blue-700 focus:outline-none focus:ring-0 active:bg-blue-800 transition duration-150 ease-in-out\" onclick=\"location.href='/motor/sub/off'\">Off</button></div></div>");
    client.println("</div>"); // <div class="basis-1/2 relative">

    // 온,습도
    client.println("<div class=\"basis-1/4\">");
    client.println("<div><span class=\"text-2xl\">온도: </span>");
    client.print("<span class=\"text-2xl font-bold\">");
    client.print(tempC); // 온도
    client.println("<span class=\"text-red-600\">°C</span></span>");
    client.println("</div>");
    client.println("<div><span class=\"text-2xl\">습도: </span>");
    client.print("<span class=\"text-2xl font-bold\">");
    client.print(humidity); // 습도
    client.println("<span>%</span></span>");
    client.println("</div>");

    client.println("<div><span class=\"text-2xl\">현재 습도: </span>");
    client.print("<span class=\"text-2xl font-bold\">");
    client.print(zaka_humidity); // 습도
    client.println("<span>%</span></span>");
    client.println("</div>");

    client.println("<div><span class=\"text-2xl\">현재 온도: </span>");
    client.print("<span class=\"text-2xl font-bold\">");
    client.print(zaka_temperature);
    client.println("<span> °C</span></span>");
    client.println("</div>");

    client.println("<div><span class=\"text-2xl\">현재 풍속: </span>");
    client.print("<span class=\"text-2xl font-bold\">");
    client.print(zaka_windSpeed);
    client.println("<span>m/s</span></span>");
    client.println("</div>");

    client.println("</div>"); // <div class="basis-1/4">
    client.println("</div>"); // <div class="flex flex-row mb-6">

    // 화분카드 선언부
    client.println("<div class=\"h-fit grid grid-cols-2 gap-4 text-center\">");
    client.println("<div class=\"border-2 border-violet-400 rounded\">");
    client.println("<span class=\"mb-1 text-2xl font-bold\">1번 화분</span><hr/>");
    client.print("<div class=\"mb-3 mt-3 text-3xl font-bold\">모터 ");
    motorOn[0]
        ? client.println("<span class=\"text-3xl font-bold\">OFF</span>")
        : client.println("<span class=\"text-3xl font-bold text-green-400\">ON</span>");
    client.println("</div>"); // <div class="mb-3 mt-3 text-3xl font-bold">

    client.println("<div class=\"mb-1 text-lg font-bold\">수분</div>");
    client.println("<div class=\"mx-auto w-9/12 h-6 bg-gray-200 rounded-full dark:bg-gray-700\">");
    client.println("<div class=\"h-6 bg-gradient-to-r from-cyan-500 to-indigo-500 rounded-full font-bold text-slate-200\" style=\"width:");
    client.print(soilPercents[0]);
    client.print("%\">");
    client.print(soilPercents[0]);
    client.println("%</div></div>");
    client.print("<div class=\"p-5 shadow-lg rounded-lg overflow-hidden\">\
                    <div class=\"py-3 px-5 bg-gray-50 font-bold\">");
    client.print(String(year) + "-" + String(month) + "-" + String(day));
    client.println(" 1번 화분 수분량</div>\
                    <canvas class=\"p-1\" id=\"chartLine1\"></canvas>\
                </div>");
    client.println("</div>");

    client.println("<div class=\"border-2 border-violet-400 rounded\">");
    client.println("<span class=\"mb-1 text-2xl font-bold\">2번 화분</span><hr/>");
    client.print("<div class=\"mb-3 mt-3 text-3xl font-bold\">모터 ");
    motorOn[1]
        ? client.println("<span class=\"text-3xl font-bold\">OFF</span>")
        : client.println("<span class=\"text-3xl font-bold text-green-400\">ON</span>");
    client.println("</div>");

    client.println("<div class=\"mb-1 text-lg font-bold\">수분</div>");
    client.println("<div class=\"mx-auto w-9/12 h-6 bg-gray-200 rounded-full dark:bg-gray-700\">");
    client.println("<div class=\"h-6 bg-gradient-to-r from-cyan-500 to-indigo-500 rounded-full font-bold text-slate-200\" style=\"width:");
    client.print(soilPercents[1]);
    client.print("%\">");
    client.print(soilPercents[1]);
    client.println("%</div></div>");

    // 차트
    client.print("<div class=\"p-5 shadow-lg rounded-lg overflow-hidden\">\
                    <div class=\"py-3 px-5 bg-gray-50 font-bold\">");
    client.print(String(year) + "-" + String(month) + "-" + String(day));
    client.println(" 2번 화분 수분량</div>\
                    <canvas class=\"p-1\" id=\"chartLine2\"></canvas>\
                </div>");

    client.println("</div></div></div>");

    // Log창
    client.println("<div class=\"m-2 mb-0 p-2 pl-4 rounded-t-lg bg-slate-400\">\
        <span class=\"text-4xl font-bold\">Log</span>\
    </div>");
    client.println("<div class=\"m-2 mt-0 p-2 pl-4 h-full bg-slate-600 font-bold text-white overflow-scroll\" style=\"height: 30vh;\">");
    for (int i = logcount; i >= 0; i--) {
        client.print("<div>");
        client.print(logs[i]);
        client.println("</div>");
    }
    client.println("</div>");
    client.println("</body>");

    // Line 차트
    client.println("<script src=\"https://cdn.jsdelivr.net/npm/chart.js\"></script>");
    client.println("<script>");


    client.print("const humidity = ");
    client.print("[[");
    for (int i = 0; i < 24; i++) {
        client.print(soilHumidity1[i]);
        client.print(",");
    }
    client.print("],[");
    for (int i = 0; i < 24; i++) {
        client.print(soilHumidity2[i]);
        client.print(",");
    }
    client.println("]]");

    client.println("const labels = [\"0시\", \"1시\", \"2시\", \"3시\", \"4시\", \"5시\", \"6시\", \"7시\", \"8시\", \"9시\", \"10시\", \"11시\", \"12시\", \"13시\", \"14시\", \"15시\", \"16시\", \"17시\", \"18시\", \"19시\", \"20시\", \"21시\", \"22시\", \"23시\"]");
    client.println("const data = [{");
    client.println("labels: labels,");
    client.println("datasets: [{");
    client.println("label: \"수분량\",");
    client.println("backgroundColor: \"hsl(252, 82.9%, 67.8%)\",");
    client.println("borderColor: \"hsl(252, 82.9%, 67.8%)\",");
    client.println("data: humidity[0],},");
    client.println("],},{");
    client.println("labels: labels,");
    client.println("datasets: [{");
    client.println("label: \"수분량\",");
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

    // html 닫기
    client.println("</html>");
}