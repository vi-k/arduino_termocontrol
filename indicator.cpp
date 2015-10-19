/***********************************************************************
 *  Работа с 4-знаковым семисегментным индикатором, подключенным
 *  напрямую к МК к портам:
 *  
 *  B0-B7 - аноды индикатора в последовательности B-G-C-Dp-D-E-A-F
 *          (PORTB: F-A-E-D-Dp-C-G-B)
 *  C2-C5 - катоды индикаторы в последовательности D4-D3-D2-D1
 *          (PORTC: x-x-D1-D2-D3-D4-x-x)
 */
#include <Arduino.h>
#include "indicator.h"


/***********************************************************************
 *  Структура для настройки режимов яркости с помощью динамической
 *  индикации:
 *    - on/off - соответственно, режимы индицации и отключения
 *      индикатора (паузы). Уровень яркости регулируется через
 *      пропорцилнальное уменьшение времени пребывания в одном
 *      и увеличения в другом.
 *      
 *    - prescaler - предделитель для TIMER2:
 *          1: 1 такт
 *          2: 8 тактов
 *          3: 32 такта
 *          4: 64 такта
 *          5: 128 тактов
 *          6: 256 тактов
 *          7: 1024 такта.
 *          
 *    - count - количество запусков обработчика до перехода
 *      к следующему знаку. Необходимо для восполнения резких
 *      переходов (в количестве тактов) между предделителями
 *      для плавных изменений яркости.
 *      
 *  Расчёт на примере 8-го уровня яркости:
 *  Параметры:
 *      {2, 1, 2, 6}
 *  Индикация:
 *      пределитель: 2
 *      кол-во запусков обработчика: 1
 *      => кол-во тактов в предделителе: 8
 *      => кол-во тактов на отображение одного знака: 8*256 = 2048
 *      => время вывода одного знака: 2048 / 8000000 = 256мкс
 *  Пауза:
 *      предделитель: 2
 *      кол-во запусков обработчика: 6
 *      => кол-во тактов на отображение одного знака:
 *          8*6*256 = 12288
 *      => время паузы: 12288 / 8000000 = 1536мкс
 *  Итого:
 *      Полный цикл отображения знаков вместе с паузой займёт:
 *          4 * 256мкс + 1536мкс = 2560мкс
 *      => Отображению одного знака в этом цикле будет уделено:
 *          256мкс / 2560мкс = 10% времени
 *          
 *  При максимальной яркости на один знак будет тратиться 25% времени.
 */

struct indicator_mode_t {
    uint8_t prescaler_on; /* Предделитель для режима горения */
    uint8_t on_count; /* Количество запусков-пропусков */
    uint8_t prescaler_off; /* Предделитель для паузы */
    uint8_t off_count;
};

const indicator_mode_t c_indicator_modes[] = {
    {4, 1, 4, 1}, /*  0 -  0.0% */
    {1, 1, 5, 1}, /*  1 -  0.8% */
    {1, 1, 4, 1}, /*  2 -  1.5% */
    {1, 2, 4, 1}, /*  3 -  2.8% */
    {1, 3, 4, 1}, /*  4 -  3.9% */
    {1, 4, 4, 1}, /*  5 -  5.0% */
    {1, 5, 4, 1}, /*  6 -  6.0% */
    {2, 1, 4, 1}, /*  7 -  8.3% */
    {2, 1, 2, 6}, /*  8 - 10.0% */
    {2, 1, 3, 1}, /*  9 - 12.5% */
    {2, 3, 4, 1}, /* 10 - 15.0% */
    {2, 2, 3, 1}, /* 11 - 16.7% */
    {2, 3, 3, 1}, /* 12 - 18.8% */
    {3, 1, 3, 1}, /* 13 - 20.0% */
    {3, 1, 2, 2}, /* 14 - 22.0% */
    {3, 1, 0, 1}  /* 15 - 25.0% */
};

const uint8_t c_max_brightness =
    sizeof(c_indicator_modes) / sizeof(*c_indicator_modes) - 1;

/* Массив изображений цифр для индикатора */
const uint8_t c_digits0_9[10] = {
    DIGIT_0, DIGIT_1, DIGIT_2, DIGIT_3, DIGIT_4,
    DIGIT_5, DIGIT_6, DIGIT_7, DIGIT_8, DIGIT_9};

indicator_t *g_one_indicator;

/***********************************************************************
 * Инициализация индикатора
 */
indicator_t::indicator_t()
    : brightness_(c_max_brightness)
{
    g_one_indicator = this;

    /* B0-B7 - аноды индикаторы */
    DDRB   = 0b11111111; /* output */
    PORTB  = 0b00000000; /* low */
    
    /* C2-C5 - катоды индикаторы */
    DDRC  |= 0b00111100; /* output */
    PORTC |= 0b00111100; /* high */
   
    /* Настраиваем таймер TIMER2 для динамической индикации */
    TCCR2A = 0; /* Используем обычный (Normal) режим работы таймера */
    TCCR2B = 1; /* Устанавливаем предделитель (для первого запуска
        самый минимальный) */
    TCNT2 = 0;
    TIMSK2 = (1 << TOIE2); /* Запускаем таймер - он будет работать
        всегда, кроме режима глубокого сна */
}

/***********************************************************************
 * Обработка переполнения счётчика TIMER2
 */
ISR(TIMER2_OVF_vect)
{  
    if (g_one_indicator)
        g_one_indicator->timer_processing();
}

/***********************************************************************
 * Динамическая индикация
 */
void indicator_t::timer_processing()
{
    if (--repeat_counter_) return;

    const indicator_mode_t &mode = c_indicator_modes[brightness_];

    /* Отключаем индикаторы (катоды к питанию) */
    PORTC |= 0b00111100;

    /* В самом ярком режиме пауза не используется */
    if (digits_n_ == 4 && mode.prescaler_off == 0)
        digits_n_ = 0;

    if (digits_n_ < 4) {
        /* Режим горения. Поочерёдно "зажигаем" знаки */
        TCCR2B = mode.prescaler_on;
        repeat_counter_ = mode.on_count;

        if (brightness_) {
            PORTB = digits_[digits_n_];
            PORTC &= ~(1 << (5 - digits_n_)) | 0b11000011; /* Нужный
                катод на землю*/
        }
            
        digits_n_++;
    }
    else {
        /*  Режим паузы. На время выключаем экран,
            чтобы уменьшить яркость */
        TCCR2B = mode.prescaler_off;
        repeat_counter_ = mode.off_count;
    
        digits_n_ = 0;
    }
}

/***********************************************************************
 * Яркость
 */
void indicator_t::set_brightness(int8_t brightness)
{
    if (brightness < 0)
        brightness_ = 0;
    else if (brightness > c_max_brightness)
        brightness_ = c_max_brightness;
    else
        brightness_ = brightness;
}

/***********************************************************************
 * Вывод числа с фиксированной запятой в память
 *  mem - адрес памяти;
 *  num - выводимое число;
 *  decimals - кол-во знаков после запятой;
 *  begin, end - начальная и конечная позициия для вывода числа
 *      (от 1 до 4);
 *  space - заполняющий символ вместо пробелов.
 */
bool indicator_t::memprint_fix(
        uint8_t *mem, int num, uint8_t decimals,
        uint8_t dig_first, uint8_t dig_last,
        uint8_t space)
{
    bool negative = false;
    int dig_with_dp = dig_last - decimals;
  
    /*  Для отрицательного числа выделяем положительную часть,
        а минус запоминаем */
    if (num < 0) {
        negative = true;
        num = -num;
    }
  
    /*  Выводим число поразрядно - от единиц и далее - пока число
        не "закончится", либо пока не закончится место для числа */
    for (int i = dig_last; i >= dig_first; i--) {
        /* В конце выводим минус */
        if (num == 0 && i < dig_with_dp && negative) {
            mem[i - 1] = SIGN_MINUS; /* Минус */
            negative = false;
        }
        /* Выводим числа поразрядно. Вместо ведущих нулей - пробелы */
        else {
            uint8_t n = (num > 0 || i >= dig_with_dp ? c_digits0_9[num % 10] : space);
            if (decimals != 0 && i == dig_with_dp) n |= SIGN_DP; /* Точка */
            mem[i - 1] = n;
        }
        num /= 10;
    }

    /* Если число не вместилось, сигнализируем об ошибке */
    return (num != 0 || negative == true ? false : true);
}

/***********************************************************************
 * Вспомогательная функция для анимации - "отправляем" знак вверх
 * Для следующего шага надо заново запустить функцию, передав ей полученный
 * результат. Всего шагов - 2. Третий шаг приведёт к пустоте.
 */
uint8_t indicator_t::anim_send_up(uint8_t digit)
{
  uint8_t res = 0;

  /* Точка просто исчезает */
  if (digit & 0b00100000) res |= 0b10000000;
  if (digit & 0b00010000) res |= 0b00000010;
  if (digit & 0b00000100) res |= 0b00000001;
  if (digit & 0b00000010) res |= 0b01000000;

  return res;
}

/***********************************************************************
 * Вспомогательная функция для анимации - "принимаем" знак снизу
 * digit - знак;
 * step - номер шага (0 - пусто; 1,2 - промежуточные шаги; 3 - сам знак).
 */
uint8_t indicator_t::anim_take_from_bottom(uint8_t digit, uint8_t step)
{
  uint8_t res = 0;
  
  /* F-A-E-D-Dp-C-G-B
   *     A(6)
   *  F(7)  B(0)
   *     G(1)
   *  E(5)  C(2)   
   *     D(4)  Dp(3)
   */
  switch (step) {
    case 0:
      res = 0;
      break;
      
    case 1:
      if (digit & 0b01000000) res |= 0b00010000;
      break;

    case 2:
      if (digit & 0b10000000) res |= 0b00100000;
      if (digit & 0b01000000) res |= 0b00000010;
      if (digit & 0b00000010) res |= 0b00010000;
      if (digit & 0b00000001) res |= 0b00000100;
      break;

    default:
      res = digit;
  }

  return res;
}

/***********************************************************************
 * Вспомогательная функция для анимации - "отправляем" знак вниз
 * Для следующего шага надо заново запустить функцию, передав ей полученный
 * результат. Всего шагов - 2. Третий шаг приведёт к пустоте.
 */
uint8_t indicator_t::anim_send_down(uint8_t digit)
{
  uint8_t res = 0;

  /* Точка просто исчезает */
  if (digit & 0b10000000) res |= 0b00100000;
  if (digit & 0b01000000) res |= 0b00000010;
  if (digit & 0b00000010) res |= 0b00010000;
  if (digit & 0b00000001) res |= 0b00000100;

  return res;
}

/***********************************************************************
 * Вспомогательная функция для анимации - "принимаем" знак сверху
 * digit - знак;
 * step - номер шага (0 - пусто; 1,2 - промежуточные шаги; 3 - сам знак).
 */
uint8_t indicator_t::anim_take_from_above(uint8_t digit, uint8_t step)
{
  uint8_t res = 0;
  
  /* F-A-E-D-Dp-C-G-B
   *     A(6)
   *  F(7)  B(0)
   *     G(1)
   *  E(5)  C(2)   
   *     D(4)  Dp(3)
   */
  switch (step) {
    case 0:
      res = 0;
      break;
      
    case 1:
      if (digit & 0b00010000) res |= 0b01000000;
      break;

    case 2:
      if (digit & 0b00100000) res |= 0b10000000;
      if (digit & 0b00010000) res |= 0b00000010;
      if (digit & 0b00000100) res |= 0b00000001;
      if (digit & 0b00000010) res |= 0b01000000;
      break;

    default:
      res = digit;
  }

  return res;
}

/***********************************************************************
 * Анимация
 * mem - массив новые значений индикатора;
 * anim_type - вид анимации;
 * step_delay - задержка между шагами анимации.
 */
void indicator_t::anim(uint8_t *mem, anim_t anim_type, uint16_t step_delay, int8_t brightness)
{
  switch (anim_type) {
    case ANIM_GOLEFT:
      {
        /* Уходим */
        for (int8_t i = 0; i < 4; i++) {
          digits_[3] = digits_[2];
          digits_[2] = digits_[1];
          digits_[1] = digits_[0];
          digits_[0] = 0;
          delay(step_delay);
          /* Как только экран очистился, переходим к следующему этапу */
          if (digits_[0] == 0 && digits_[1] == 0 && digits_[2] == 0 && digits_[3] == 0) break;
        }

        /* Ищем последний значимый символ в новом значении */
        int8_t last_mem = -1;
        for (int i = 3; i >= 0; i--) {
          if (mem[i] != 0) {
            last_mem = i;
            break;
          }
        }
         
        /* Приходим */
        if (brightness >= 0) set_brightness(brightness);
        
        for (int8_t i = 0; i < 4; i++) {
          for (int8_t j = 0; j <= i; j++) {
            int8_t index = last_mem - i + j;
            digits_[j] = (index < 0 ? 0 : mem[index]);
          }
          delay(step_delay);
        }
      }
      break;

    case ANIM_GORIGHT:
      {      
        /* Уходим */
        for (int i = 0; i < 4; i++) {
          digits_[0] = digits_[1];
          digits_[1] = digits_[2];
          digits_[2] = digits_[3];
          digits_[3] = 0;
          delay(step_delay);
          if (digits_[0] == 0 && digits_[1] == 0 && digits_[2] == 0 && digits_[3] == 0) break;
        }

        /* Ищем первый значимый символ в новом значении */
        int8_t first_mem = 4;
        for (int i = 0; i <= 3; i++) {
          if (mem[i] != 0) {
            first_mem = i;
            break;
          }
        }

        /* Приходим */
        if (brightness >= 0) set_brightness(brightness);

        for (int i = 0; i < 4 - first_mem; i++) {
          for (int j = 0; j <= i; j++) {
            digits_[3 - i + j] = mem[first_mem + j];
          }
          delay(step_delay);
        }
      }
      break;
    
    case ANIM_GODOWN:
      {
        /* Уходим */
        for (int i = 0; i < 3; i++) {
          for (int j = 0; j < 4; j++) {
            digits_[j] = anim_send_up(digits_[j]);
          }
          delay(step_delay);
        }
        
        /* Приходим */
        if (brightness >= 0) set_brightness(brightness);

        for (int i = 1; i <= 3; i++) {
          for (int j = 0; j < 4; j++) {
            digits_[j] = anim_take_from_bottom(mem[j], i);
          }
          delay(step_delay);
        }
      }
      break;
      
   case ANIM_GOUP:
      {
        /* Уходим */
        for (int i = 0; i < 3; i++) {
          for (int j = 0; j < 4; j++) {
            digits_[j] = anim_send_down(digits_[j]);
          }
          delay(step_delay);
        }
      
        /* Приходим */
        if (brightness >= 0) set_brightness(brightness);

        for (int i = 1; i <= 3; i++) {
          for (int j = 0; j < 4; j++) {
            digits_[j] = anim_take_from_above(mem[j], i);
          }
          delay(step_delay);
        }
      }
      break;
    
    default:
        for (int i = 0; i < 4; i++) {
          digits_[i] = mem[i];
        }
  } /* switch (anim_type) */
}

