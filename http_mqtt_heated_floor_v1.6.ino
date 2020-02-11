
#include <SPI.h>
#include <Ethernet2.h>
#include <PubSubClient.h> //Библиотека для работы с MQTT

#define B 3988 // B-коэффициент
#define SERIAL_R 10000 // сопротивление последовательного резистора
#define THERMISTOR_R 10000 // номинальное сопротивления термистора
#define NOMINAL_T 25 // номинальная температура
#define THERMISTOR_R_68 6800
/*
  Простейший фильтр: запаздывающий, бегущее среднее, "цифровой фильтр", фильтр низких частот - это всё про него любимого
  Имеет две настройки: постоянную времени FILTER_STEP (миллисекунды), и коэффициент "плавности" FILTER_COEF
  Данный фильтр абсолютно универсален, подходит для сглаживания любого потока данных
  При маленьком значении FILTER_COEF фильтрованное значение будет меняться очень медленно вслед за реальным
  Чем больше FILTER_STEP, тем меньше частота опроса фильтра
  Сгладит любую "гребёнку", шум, ложные срабатывания, резкие вспышки и прочее...
*/
//постоянные для фильтрации
#define FILTER_STEP 200
#define FILTER_COEF 0.03

//int val_1,val_2,val_3,val_4,val_5,val_6; //Сырые значения принятые из АЦП
int val[7] = {0,0,0,0,0,0,0}; //Сырые значения принятые из АЦП
float val_f[7] = {0,0,0,0,0,0,0}; //Отфильтрованные
unsigned long filter_timer;
unsigned long status_timer;
unsigned long send_timer;
char tmp[10];
char tmp1[10];
  unsigned char* OutputStr = new unsigned char[50];
  String BufStr = "";
//-------------------------------------------------------------------------------------------------
     
#define Transmit 2            //Выход для включения передачи
#define ErrorLED 9             //Выход на включение канала ошибки
#define sensor_temp_1 A0      //датчик 1 канала
#define sensor_temp_2 A1      //датчик 2 канала
#define sensor_temp_3 A2      //датчик 3 канала
#define sensor_temp_4 A3      //датчик 4 канала
#define sensor_temp_5 A4      //датчик 5 канала
#define sensor_temp_6 A5      //датчик 6 канала

//Время через которое отправлять показания по MQTT
#define TimeSend 10000 //Каждые 10 секунд

#define ID_CONNECT "Heater_Floor"

int Floor[7] = {0,3,4,5,6,7,8};  //Номера выходов на реле
int Floor_status[7] = {0,0,0,0,0,0,0};  //Статус разрешения включения теплого пола 0 - выключено, 1 - включено
int Blocked[7] = {0,0,0,0,0,0,0};  //Блоктровка включения пола если что то произошло

//Определим температуру по умолчанию равной 22 градуса
float Set_temp[7] = {22.0,22.0,22.0,22.0,22.0,22.0,22.0};

float temp[7] = {0.0,0.0,0.0,0.0,0.0,0.0,0.0};

// введите ниже MAC-адрес и IP-адрес вашего контроллера;
// IP-адрес будет зависеть от вашей локальной сети:
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress server_mqtt(192,168,1,130);
IPAddress ip(192,168,1,111);
     

     
// задаем переменные для клиента:
char linebuf[150];
int charcount=0;

//**************************************************************
//******** Процедура обработки входящих топиков*****************
//**************************************************************
void callback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  String strTopic = String(topic);
  String strPayload = String((char*)payload);

  //обработка принятых сообщений
  if ((strTopic == "Heater_Floor/Set_Temp") || (strTopic == "Heater_Floor/Set_Chanel"))
      {
//      Serial.println("Input valve_1 = " + strPayload);
      //Servo_Valve_1.write(map(strPayload.toInt(),0,100,0,180));
      Parsing_string(strPayload);
      }

}
//**************************************************************

// инициализируем библиотеку Ethernet Server, указывая нужный порт
// (по умолчанию порт для HTTP – это «80»):
EthernetServer server(80);
EthernetClient ethClient;
PubSubClient client_mqtt(server_mqtt, 1883, callback, ethClient);

//Процедура переподключения
void reconnect() {
  while (!client_mqtt.connected()) {
    if (client_mqtt.connect(ID_CONNECT)) {
      client_mqtt.subscribe("Heater_Floor/#");
      client_mqtt.publish("Heater_Floor/Info", "Reconnect");
    } else {
//      Serial.println("SERVER CONNECTION IS LOST!!!");
      delay(5000);
    }
  }
}
     
void setup() 
{
  pinMode(Transmit, OUTPUT);
  pinMode(sensor_temp_1, INPUT);
  pinMode(sensor_temp_2, INPUT);
  pinMode(sensor_temp_3, INPUT);
  pinMode(sensor_temp_4, INPUT);
  pinMode(sensor_temp_5, INPUT);
  pinMode(sensor_temp_6, INPUT);
  pinMode(Floor[1], OUTPUT);
  pinMode(Floor[2], OUTPUT);
  pinMode(Floor[3], OUTPUT);
  pinMode(Floor[4], OUTPUT);
  pinMode(Floor[5], OUTPUT);
  pinMode(Floor[6], OUTPUT);
  pinMode(ErrorLED, OUTPUT);
  digitalWrite(Transmit, LOW);
  digitalWrite(Floor[1], LOW);
  digitalWrite(Floor[2], LOW);
  digitalWrite(Floor[3], LOW);
  digitalWrite(Floor[4], LOW);
  digitalWrite(Floor[5], LOW);
  digitalWrite(Floor[6], LOW);
  digitalWrite(ErrorLED, LOW);
     
      // открываем последовательную коммуникацию на скорости 9600 бод:
//      Serial.begin(9600);
     
      // запускаем Ethernet-коммуникацию и сервер:
      Ethernet.begin(mac, ip);
      server.begin();
//      Serial.print("server is at ");  //  "сервер на "
//      Serial.println(Ethernet.localIP());
}

//=====================================================================================================================
// Генерируем веб-страницу с кнопкой «вкл/выкл» для реле:
//=====================================================================================================================

void dashboardPage(EthernetClient &client) {
          client.println(F("HTTP/1.1 200 OK"));
          client.println(F("Content-Type: text/html"));
          client.println(F("Connection: close"));  // the connection will be closed after completion of the response
          //client.println(F("<?xml version= 1.0 encoding= UTF-8 ?>"));
          //client.println(F("<!DOCTYPE html PUBLIC "));
          //client.println("Refresh: 5"));  // refresh the page automatically every 5 sec
          client.println();
          client.println(F("<!DOCTYPE HTML>"));
          client.println(F("<html>"));
          client.println(F("  <br>"));
          client.println(F(" <hr align= center  width= 300  size= 3 color= #0000dd  /> "));
          client.println(F(" <b> <center> 6-CHANNEL HEATED FLOOR <center> </b>"));
          client.println(F(" <hr align= center  width= 300  size= 3 color= #0000dd  /> "));
          client.println(F("<br>"));
          client.println(F(" <form>"));
          //-------------------------------------------------------------------------------------------------------------
          for (int p=1; p<=6; p++)
            {
              client.println(" &nbsp;  SET TEMP "+String(p)+": &nbsp;");
              client.print("<input type= number  name= TMP_"+String(p)+"  max= 30 min= 22  step= 0.5 value= "); // окно ввода
              client.print(String(Set_temp[p]));
              client.print(" style= width:100px </p>"); // окно ввода
              client.print("<input name= CHN_"+String(p)+" type= radio value= 1");
                if (Floor_status[p]) client.print(" checked");
              client.print(" >  ON");
              client.print("<input name= CHN_"+String(p)+" type= radio value= 0");
                if (!Floor_status[p]) client.print(" checked");
              client.print(" >  OFF");
              client.println("<br>");
            }
          //--------------------------------------------------------------------------------------------------------------
          client.println(F(" <hr align= center  width= 300  size= 3  color= #dddddd  /> "));
          client.println(F("<p><input type= submit  value= SEND  ></p>"));
          client.println(F("</html>"));
}

void InfoPage(EthernetClient &client) {
          client.println(F("HTTP/1.1 200 OK"));
          client.println(F("Content-Type: text/html"));
          client.println(F("Connection: close"));  // the connection will be closed after completion of the response
//          client.println(F("<?xml version= 1.0 encoding= UTF-8 ?>"));
//          client.println(F("<!DOCTYPE html PUBLIC "));
          //client.println("Refresh: 5"));  // refresh the page automatically every 5 sec
          client.println();
          client.println(F("<!DOCTYPE HTML>"));
          client.println(F("<html>"));
          //client.println(F("  <br>"));
          for (int j=1;j<7;j++){
                  client.println(F("  <br>"));
                  client.print("TMP_SET"+String(j)+" = "+String(Set_temp[j])+"   ");
                  client.print("TMP_REAL"+String(j)+" = "+String(temp[j])+"   ");
                  client.print("STAT"+String(j)+" = ");
                  if (Floor_status[j]) {
                    client.print("\"ON\"");
                  }else{
                    client.print("\"OFF\"");
                  }
          }
          //client.println(String(linebuf));
          client.println(F("</html>"));
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------------
//------------------- Процедура парсинга строки ---------------------------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------------------------------------------------------------------------
//http://192.168.1.111/?TMP_1=22.00&CHN_1=0&TMP_2=24&CHN_2=1&TMP_3=22.00&CHN_3=0&TMP_4=22.00&CHN_4=0&TMP_5=24&CHN_5=1&TMP_6=22.00&CHN_6=0
//-------------------------------------------------------------------------------------------------------------------------------------------------------------------
void Parsing_string(String input_str){
//Начинаем перебирать по всем каналам управления
for (int j=1;j<=6;j++){
   for (int i=0;i<input_str.length();){
     if (input_str.substring(i).startsWith("TMP_"+String(j)+"=")) 
     {
      //Serial.println("TMP_"+String(j)+"=");
        i+=6;
        int s=i; 
        //Ищем нужные символы (0x30 - 0x39) - цифры, 0x2E - точка
        while((i<input_str.length())&&((input_str.charAt(i)>=0x30)&&(input_str.charAt(i)<=0x39))||(input_str.charAt(i)==0x2E)) {
           ++i;
        }
        String temp_str = input_str.substring(s,i);
        temp_str.toCharArray(tmp, temp_str.length()+1);
        float float_temp = atof(tmp);
        if ((float_temp>=22) && (float_temp<=30)){
          Set_temp[j] = float_temp;
        }
     }
     if (input_str.substring(i).startsWith("CHN_"+String(j)+"=")) 
     {
      //Serial.println("CHN_"+String(j)+"=");
        i+=6;
        int b=i; 
        //Ищем нужные символы, только буквенные
        while((i<input_str.length())&&((input_str.charAt(i)>=0x30)&&(input_str.charAt(i)<=0x31))) {
           ++i;
        }
        String floor_str = input_str.substring(b,i);
        Floor_status[j] = floor_str.toInt();
//        Serial.println(temp_str_2);
//        temp_str.toCharArray(tmp, temp_str.length()+1);
//        Set_temp[j]=atof(tmp);
     }
     ++i;
   }
}
}
//----------------------------------------------------------------------------------------------------------------------------------------------------------------------
void loop() {
  
// прослушиваем входящих клиентов:
EthernetClient client = server.available();
  if (client) {
//     Serial.println("new client");  //  "новый клиент"
     //Выделяем память для клиента
     memset(linebuf,0,sizeof(linebuf));
     charcount=0;
     // HTTP-запрос заканчивается пустой строкой:
     boolean currentLineIsBlank = true;
     while (client.connected()) {
        if (client.available()) {
           char c = client.read();
            // считываем HTTP-запрос, символ за символом:
            linebuf[charcount]=c;
            if (charcount<sizeof(linebuf)-1) charcount++;
            // если вы дошли до конца строки (т.е. если получили
            // символ новой строки), это значит,
            // что HTTP-запрос завершен, и вы можете отправить ответ:
            if (c == '\n' && currentLineIsBlank) {
              //Parsing_string(String(linebuf));
              dashboardPage(client);
              break;
            }
            if (c == '\n') {
              Parsing_string(String(linebuf));
              //InfoPage(client);
              if (strstr(linebuf,"GET /info") > 0){
                  InfoPage(client);
                  break;
//                digitalWrite(relay, LOW);
//                relay1State = "Off";
              }
              else if (strstr(linebuf,"GET /all_off") > 0){
//                digitalWrite(relay, HIGH);
///                relay1State = "On";
              }
              // если получили символ новой строки...
              currentLineIsBlank = true;
              memset(linebuf,0,sizeof(linebuf));
              charcount=0;          
            }
            else if (c != '\r') {
              // если получили какой-то другой символ...
              currentLineIsBlank = false;
            }
          }
        }
        // даем веб-браузеру время на получение данных:
        delay(1);
        // закрываем соединение:
        client.stop();
//        Serial.println("client disonnected");  //  "Клиент отключен"
     }
//Проверяем подключены ли к серверу MQTT
if (!client_mqtt.connected()) {
    reconnect();
  }
  
  client_mqtt.loop();//Цикличная обработка соединения с сервером MQTT
//**************************************************************
//******Выполняется всегда, вне зависимости от подключения *****
//**************************************************************

if (millis() - filter_timer > FILTER_STEP) 
  {
    filter_timer = millis();    // сброс таймера
    // читаем значение (не обязательно с аналога, это может быть ЛЮБОЙ датчик)
    val[1] = analogRead(sensor_temp_1);
    val[2] = analogRead(sensor_temp_2);
    val[3] = analogRead(sensor_temp_3);
    val[4] = analogRead(sensor_temp_4);
    val[5] = analogRead(sensor_temp_5);
    val[6] = analogRead(sensor_temp_6);
    // основной алгоритм фильтрации. Внимательно прокрутите его в голове, чтобы понять, как он работает
    for (int i=1; i <= 6; i++)
    {
      val_f[i] = val[i] * FILTER_COEF + val_f[i] * (1 - FILTER_COEF);
    }

    temp[1] = convert_temp(val_f[1]);
    temp[2] = convert_temp_68(val_f[2]);
    temp[3] = convert_temp(val_f[3]);
    temp[4] = convert_temp(val_f[4]);
    temp[5] = convert_temp(val_f[5]);
    temp[6] = convert_temp_68(val_f[6]);

    for (int f=1; f<=6; f++)
      {
        if ((temp[f]<Set_temp[f]) && (Floor_status[f]) && (!Blocked[f])) digitalWrite( Floor[f], HIGH);
        if ((temp[f]>(Set_temp[f]+0.5)) || (!Floor_status[f]) || (Blocked[f])) digitalWrite( Floor[f], LOW);
      }
  }
//***********************************************************************************************

 //Проверка на обрыв или короткое замыкание датчика примерно раз в секунду при условии включенного нагрева
 //если нагрев выключен проверка не ведется
if (millis() - status_timer > 1000) 
  {
    status_timer = millis();    // сброс таймера
    for (int g=1; g<7; g++){
      //Если температура меньше 10 или выше 40 и включен нагрев, то включаем блокировку канала
      if (((temp[g]<10) || (temp[g]>40)) && (Floor_status[g])) {
        Blocked[g] = 1;
      }else{
        Blocked[g] = 0;
      }
    }
    if (Blocked[1] || Blocked[2] || Blocked[3] || Blocked[4] || Blocked[5] || Blocked[6]) {
      if (digitalRead(ErrorLED) == LOW)
          {
            digitalWrite( ErrorLED, HIGH);
          } else{
            digitalWrite( ErrorLED, LOW);
          }
    }
  }

//Отправка значений по MQTT
if (millis() - send_timer > TimeSend) 
  {
    send_timer = millis();    // сброс таймера
    BufStr = "";
    for (int g=1; g<7; g++){
      dtostrf(temp[g], 2, 2, tmp1);
      //-------------------------------------------------------
      BufStr = "Heater_Floor/Real_Temp_"+String(g);
      BufStr.getBytes(OutputStr, 50, 0);
      //-------------------------------------------------------
      client_mqtt.publish(OutputStr, tmp1);
      delay(50);
      BufStr = "Heater_Floor/Status_CHN_"+String(g);
      BufStr.getBytes(OutputStr, 50, 0);
        if (Floor_status[g])
          {
            client_mqtt.publish(OutputStr, "ON");
          }
         else
          {
            client_mqtt.publish(OutputStr, "OFF");
          }
      delay(50);
    }
  }
delay(1);
}
//**************************************************************

//Конвертирование температуры с датчика 10кОм
float convert_temp(float in){
    //Вычисляем сопротивление
    float tr = 1023.0 / in - 1;
    tr = SERIAL_R / tr;
    //Serial.print("R=");
    //Serial.print(tr);
    //Serial.print(", t=");
    //Исходя из сопротивления вычисляем температуру
    float steinhart;
    steinhart = tr / THERMISTOR_R; // (R/Ro)
    steinhart = log(steinhart); // ln(R/Ro)
    steinhart /= B; // 1/B * ln(R/Ro)
    steinhart += 1.0 / (NOMINAL_T + 273.15); // + (1/To)
    steinhart = 1.0 / steinhart; // Invert
    steinhart -= 273.15; 
    if (steinhart<0) {return 0;}
    //float temp = steinhart;
    return steinhart;
}

//Конвертирование температуры с датчика 6.8кОм
float convert_temp_68(float in_68){
    //Вычисляем сопротивление
    float tr_68 = 1023.0 / in_68 - 1;
    tr_68 = SERIAL_R / tr_68;
    //Serial.print("R=");
    //Serial.print(tr);
    //Serial.print(", t=");
    //Исходя из сопротивления вычисляем температуру
    float steinhart_68;
    steinhart_68 = tr_68 / THERMISTOR_R_68; // (R/Ro)
    steinhart_68 = log(steinhart_68); // ln(R/Ro)
    steinhart_68 /= B; // 1/B * ln(R/Ro)
    steinhart_68 += 1.0 / (NOMINAL_T + 273.15); // + (1/To)
    steinhart_68 = 1.0 / steinhart_68; // Invert
    steinhart_68 -= 273.15; 
    if (steinhart_68<0) {return 0;}
    //float temp = steinhart;
    return steinhart_68;
}
