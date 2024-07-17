
#include <INA226.h>  // INA Library
#include <SPI.h>
#include <TFT_ST7735.h>

TFT_ST7735 tft = TFT_ST7735();
//#define TFT_CS 10 // Chip select control pin
//#define TFT_DC  7  // Data Command control pin
//#define TFT_RST 9
const uint32_t SHUNT_MICRO_OHM = 10000;  ///< Сопротивление шунта в микроомах, например 100000 - это 0,1 Ом
const uint16_t MAXIMUM_AMPS    = 11;       ///< Максимальное  измеряемое значение тока, значения 1 А - ограничено до 1022 А
//INA_Class      INA;
INA226_Class INA226;                      ///< INA class instantiation
int  V_max = 36 ;   //
int A_max = 10; //
float  Temp_min = 30 ; // Минимальная температура при которой начнет работать ШИМ вентилятора.
float  Temp_max = 60 ; // Температура при которой скорость вентилятора будет максимальной.
//-------Здесь хранятся все переменные
float  temperature = 0 ;
float  V,A, W , mAh , Wh ;
int   V_graf , A_graf , PWM_out ;
int   PWM = 128 ;
unsigned long  new_Millis ;

// Переменные для расчета ШИМА
float input_W = 0; // Текущее значение Ватт
float last_input_W = 0; // Прошлое значение Ватт
int shim_shift_steps = 1; // Количество шагов на которые изменяется шим за раз
int forward_direction = 0; // "Направление" изменения ШИМА. 0 - "влево", 1 - "вправо"
int delayPwmUpdate = 100; // Задержка обновления ШИМ
unsigned long currentTime = 0;
unsigned long lastTimePwmUpdated = 0; // Тут хранится время последнего обновления ШИМ

// Получение противоположного значения направления изменения шима
int changeDirection(int value) {
  if (0 == value) return 1;
  return 0;
}

#define B 3950 // B-коэффициент
#define SERIAL_R 100000 // сопротивление последовательного резистора, 100 кОм
#define THERMISTOR_R 100000 // номинальное сопротивления термистора, 100 кОм
#define NOMINAL_T 25 // номинальная температура (при которой TR = 100 кОм)
const byte tempPin = 34 ;//  в новой варсии плати A1

void setup() {
  Serial.begin(9600);
  TCCR2B = TCCR2B & 0b11111000 | 0x01;     //Включаем частоту ШИМ'а  вентилятора на ногах 3 и 11: 31250 Гц. Это позволит избавиться от неприятного писка в работе вентилятора.

  pinMode( tempPin, INPUT );
  INA226.begin( MAXIMUM_AMPS, SHUNT_MICRO_OHM);  // Set to the expected Amp maximum and shunt resistance
  
  INA226.setBusConversion(8244);             // Время конверсии в микросекундах (140,204,332,588,1100,2116,4156,8244)8244µs=8.244 ms
  INA226.setShuntConversion(8244);           // Время конверсии в микросекундах (140,204,332,588,1100,2116,4156,8244)8244µs=8.244 ms
  INA226.setAveraging(4);                  // Среднее количество чтений n раз (1,4,16,64,128,256,512,1024)
  INA226.setMode(INA_MODE_CONTINUOUS_BOTH);  // Шина / шунт измеряется постоянно
 // INA.alertOnBusOverVoltage(true, 5000);  // Уведомление про спусковой сигнал, если  более 5В

  tft.init();                 // Инициализация дисплея.
  tft.setRotation(1);         // Переворачиваем дисплей.
  tft.fillScreen(TFT_BLACK);  // Указываем цвет заливки дисплея
  //----- Рисуем рамку.
  tft.fillRect(-1, -2,160, 128, ST7735_LIGHTGREY);
  tft.fillRect(2, 1, 154, 122, ST7735_BLACK);
  tft.fillRect(-1, 109, 160, 3, ST7735_LIGHTGREY);
  //-----В этом месте все статические данные, которые будут отображаться на дисплее
  tft.setTextColor(ST7735_RED, ST7735_BLACK);      // ( цвет текста , цвет заливки текста )
  tft.drawRightString("Amp", 145, 85, 2);           // ( "Текст" , положение по оси Х , положение по оси Y , размер шрифта)
  tft.setTextColor(ST7735_YELLOW, ST7735_BLACK  );
  tft.drawRightString("Watt", 147, 8, 2);
  tft.setTextColor(ST7735_WHITE, ST7735_BLACK);
  tft.drawRightString("mAh", 37, 18, 1);
  tft.drawRightString("Temp ", 35, 114, 1);
  tft.drawRightString("PWM", 123, 114, 1);
  tft.setTextColor(ST7735_ORANGE, ST7735_BLACK);
  tft.drawRightString("%", 155, 114, 1);


   while (INA226.begin(MAXIMUM_AMPS, SHUNT_MICRO_OHM) == 0) {
    tft.setTextColor(ST7735_RED, ST7735_BLACK);
    tft.drawRightString("Eror INA229 " , 150, 45, 4);
       delay(300);
  tft.fillRect(5, 40, 150, 31,ST7735_BLACK) ;
       delay(300);
   }
  //-----

  new_Millis = millis();

}
void loop() {
  //----- Расчет всех динамических данных.------
 V = INA226.getBusMilliVolts() / 10e2;
 A = INA226.getBusMicroAmps()  /10e3;
 //input_W = INA226.getBusMicroWatts() / 10e5;
 W = V*A;
 input_W = W;
 if (V<0){V=0;}
   if (A<0){A=0;}
    if (W<0){W=0;}
  mAh += A * (millis() - new_Millis) / 3600000 * 1000; //расчет емкости  в мАч
  new_Millis = millis();

  if (new_Millis - lastTimePwmUpdated > delayPwmUpdate) {
    // Расчет шима в зависимости от Ватт
    // Расчет "направления" сдвига значения шима
    if (input_W < last_input_W) { // Если текущие Ватты меньше предыдущих - мы идем в неверном направлении. Меняем направление
      forward_direction = changeDirection(forward_direction);
    }

    // Если прошлое и значение значение Ватт разное - корректируем ШИМ
    if (input_W != last_input_W) {

      if (forward_direction == 0) {
        PWM = PWM - shim_shift_steps; // понижаем ШИМ если идем "влево"
      }

      if (forward_direction == 1) {
        PWM = PWM + shim_shift_steps; // понижаем ШИМ если идем "влево"
      }
    }

    // Корректируем значение ШИМЮ, чтобы он не выходил за пределы требуемого диапазона
    if (PWM < 0) PWM = 0;
    if (PWM > 255) PWM = 255;

    // Сохраняем значение Ватт для следующей итерации
    last_input_W = input_W;
    lastTimePwmUpdated = new_Millis;
  }

  // Определяем температуру на датчике.

  int t = analogRead( tempPin );
  float tr = 1023.0 / t - 1;
  tr = SERIAL_R / tr;
  float temperature;
  temperature = tr / THERMISTOR_R; // (R/Ro)
  temperature = log(temperature); // ln(R/Ro)
  temperature /= B; // 1/B * ln(R/Ro)
  temperature += 1.0 / (NOMINAL_T + 273.15); // + (1/To)
  temperature = 1.0 / temperature; // Invert
  temperature -= 273.15;
  //Рассчитываем  ШИМ вентилятора.
//   if (temperature >= Temp_min && temperature <= Temp_max )  {
//     PWM = ( temperature - Temp_min ) * 255 / ( Temp_max - Temp_min ); }
//   else if (temperature < Temp_min)  { PWM = 0;}
//   else if (temperature >= Temp_max)  {PWM = 255;}

  //PWM = map(temperature,Temp_min, Temp_max, 30, 255);
//----- Отображение всех динамических данных.------
  char V_out[7]; dtostrf( V , 5, 2, V_out);
  char A_out[8]; dtostrf( A , 7, 2, A_out);
  char W_out[8]; dtostrf( W , 7, 2, W_out);
  char Temperature[4]; dtostrf(temperature, 3, 0, Temperature);
  char PWM_out[4]; dtostrf(PWM / 2.55, 3, 0, PWM_out);
  char mAh_out[8];

  tft.setTextColor( ST7735_WHITE, ST7735_BLACK);
  tft.drawRightString(V_out, 140, 35, 6);
  if (V < 10)  {tft.drawRightString("  ", 35, 35, 6);}

  tft.setTextColor(ST7735_RED, ST7735_BLACK );
  dtostrf( A ,8, 3, A_out);
  tft.drawRightString(A_out, 110, 83, 4);
 

    
  
  tft.setTextColor(ST7735_YELLOW, ST7735_BLACK );
 
   if (W < 99.99) {
    dtostrf(W , 6, 2, W_out);
    tft.drawRightString(W_out, 110, 8, 4);
  }
  else if (W >= 100 )   {
    dtostrf( W , 6, 1, W_out);
    tft.drawRightString(W_out, 110, 8, 4);
  }
 // if (W <= 9.99) { tft.fillRect(45, 5, 15, 25,ST7735_BLACK) ;}
  
  
  //tft.drawRightString(W_out, 110, 7, 4);
 // if (W < 100)  {tft.drawRightString("  ", 46, 7, 4);}

  tft.setTextColor(ST7735_ORANGE, ST7735_BLACK);
  tft.drawRightString(Temperature, 50 , 114, 1);
  tft.drawRightString(PWM_out , 148, 114, 1);

 
 tft.setTextColor(ST7735_WHITE, ST7735_BLACK);
  if (mAh < 100) {
    dtostrf( mAh , 4, 2, mAh_out);
    tft.drawRightString(mAh_out, 41 , 9, 1);
  }
  else if (mAh >= 100 && mAh < 1000 )   {
    dtostrf( mAh , 5, 1, mAh_out);
    tft.drawRightString(mAh_out, 41, 9, 1);
  }
  else  if  (mAh >= 1000 && mAh < 10000) {
    itoa (mAh, mAh_out, 10);
    dtostrf( mAh , 5, 0, mAh_out);
    tft.drawRightString(mAh_out, 40, 9, 1);}
  else  if  (mAh >= 10000 ) {dtostrf( mAh , 6, 0, mAh_out);
    tft.drawRightString(mAh_out, 45, 9, 1);
    }

  //-----
  //----- Отображение шкал заполнения.
  V_graf = V / V_max * 100 ;
  if (V_graf < 0) {V_graf = -V_graf;}
  tft.fillRect(5, 5, 4, 100- V_graf, ST7735_BLACK);
  tft.fillRect(5, 105 - V_graf, 4, V_graf+1, ST7735_BLUE);

  A_graf = A / A_max * 100 ;
  if (A_graf < 0) {A_graf = -A_graf;}
  tft.fillRect(150, 6, 4, 100- A_graf, ST7735_BLACK);
  tft.fillRect(150, 106 - A_graf, 4, A_graf+1, ST7735_RED);
  //-----
  //Serial.print("Temperature");
   // Serial.println(temperature);
   /* Serial.print   ("  A " );
    Serial.print   ( A  , 4 );
    Serial.print   ("  V " );
    Serial.print   (  V , 3);
    Serial.print   ("  W_ina " );
    Serial.print   ( W  , 4 );
    Serial.print   ("  W=V*A " );
    Serial.print   ( A*V  , 4 );
    Serial.print   ( "   % "  );
    Serial.println ((W/(V*A))*100 );
    */
    Serial.println   ( temperature  );
     Serial.println   (PWM  );
   // Serial.print   ( INA226.getBusMicroAmps() );
  analogWrite(3, PWM);
  delay (10);
}
