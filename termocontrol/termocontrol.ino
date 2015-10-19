/***********************************************************************
 *  Контроллер температуры, вариант из общего шаблона.
 *  (c) Дунаев В.В., 2015
 *  
 *  Состоит из двух блоков:
 *  1) На основе обычного термометра. Нет блока питания. Добавлены
 *     пьезопищалка и шина RJ-11 для связи со вторым блоком и получения
 *     от него питания;
 *  2) Блок с:
 *     - преобразователем питания (переменные 220В в постоянные 5В)
 *       и соответствующей вилкой для подключения к сети 220В;
 *     - реле для управления нагревателем и соответствующей розеткой
 *       для подключения нагревателя;
 *     - двумя термодатчиками: внешним (для контроля температуры
 *       во внешней среде) и внутренним (внутри первого блока для
 *       измерения комнатной температуры);
 *     - двумя светодиодами для визуального контроля работы блока
 *       (зелёный - включен в сеть, питание 5В поставляется; красный -
 *       нагреватель включен);
 *     - каналом связи RJ-11 для связи с первым блоком и передачи
 *       питания ему.
 *
 *  Описание портов:
 *  B0-B7 - аноды индикатора в последовательности B-G-C-Dp-D-E-A-F
 *          (для PORTB: F-A-E-D-Dp-C-G-B)
 *  C2-C5 - катоды индикаторы в последовательности D4-D3-D2-D1
 *          (для PORTC: x-x-D1-D2-D3-D4-x-x)
 *  D0-D3 - кнопки в последовательности [1]-[2]-[3]-[4]
 *          (для PORTD: x-x-x-x-[4]-[3]-[2]-[1])
 *  D4    - канал управления реле 220В
 *  D6    - пьезоизлучатель звука BUZZER
 *          (для PORTD: x-BUZ-x-x-x-x-x-x)
 *  D7    - температурные датчики DS18B20
 *          (для PORTD: DS-x-x-x-x-x-x-x)
 */

/* Включение/выключение нагревателя */
#define HEATER_ON()   do { PORTD |= 0b00010000; } while(0)
#define HEATER_OFF()  do { PORTD &= 0b11101111; } while(0)

/* Включение/выключение пьезопищалки */
#define BUZZER_ON()   do { PORTD |= 0b01000000; } while(0)
#define BUZZER_OFF()  do { PORTD &= 0b10111111; } while(0)

#include <OneWire.h>
#include <LowPower.h>
#include "termocontrol.h"
#include "indicator.h"

indicator_t g_indicator;
mode_t g_mode = MODE_SENSOR1; /* Режим индикации */
uint8_t g_errno; /* Ошибка */

OneWire g_sensors(7); /* Порт D7 на Arduino = D7 на Atmega328p */
uint8_t g_sensors_addr[2][8]; /* Адреса датчиков */
int8_t g_control_sensor = -1; /* Номер датчика с контролем температуры
                                 (-1 - не определён) */
int g_control_temp = 0; /* Температура для контроля */
bool g_control_actived; /* Флаг активности контроля */
unsigned long g_mode_control_timestamp; /* Метка времени нахождения
                                           в режиме MODE_CONTROL */
unsigned long g_blink_timestamp; /* Метка времени для анимации в режиме
                                    контроля температуры */
uint8_t g_blink_step; /* Шаг мигания */

unsigned long g_poll_timestamp; /* Метка времени опроса датчиков */


/***********************************************************************
 * Состояние кнопок
 */

/* Фактическое состояние кнопок ("железа") для определения
    нажатий/отжатий: 0 - нажата, 1 - отжата */
uint8_t g_buttons_hard_state = 0b1111;
/* Состояние кнопок, используемых для комбинаций (т.н. контрольные
    кнопки - по аналогии с кнопкой Ctrl на ПК): 1 - нажата, 0 - отжата */
uint8_t g_buttons_ctrl_state = 0b0000;
/* Номер нажатой кнопки - последней нажатой кнопки, т.е. той кнопки,
    на которую будет реакция программы: 0 - нет нажатой кнопки,
    1-4 - номер кнопки (слева направо) */
uint8_t g_pressed_button = 0;
/* Штамп времени нажатия или последней обработки кнопки (для режима
    многократных повторов) */
unsigned long g_pressed_timestamp = 0;
/* Первый штамп (многократные повторы начинаются не сразу) */
bool g_pressed_timestamp_first = false;


/***********************************************************************
 *  Обработка прерывания от нажатия кнопок на портах D0 (PCINT16),
 *  D1 (PCINT17), D2 (PCINT18) и D3 (PCINT19)
 */
ISR(PCINT2_vect)
{
    /* Нам нужно только просыпаться. Всё остальная обработка
        в рабочем цикле */
}

/***********************************************************************
 *  Задержка с проверкой нажатия кнопок
 *  Работает без сложностей: разбивает заданное время на промежутки
 *  по 50мс и проверяет изменение порта между паузами.
 */
bool delay_with_test_buttons(uint16_t time, bool break_if_pressed)
{
    bool test = false;

    while (time > 0) {
        if ((PIND & 0b1111) != 0b1111) {
            test = true;
            if (break_if_pressed) break;
        }
        delay(50);
        time -= 50;
    }

    return test;
}

/***********************************************************************
 *  Запуск температурных датчиков на конвертацию
 */
void convertT()
{
    g_sensors.reset();
    g_sensors.write(0xCC); /* SKIP ROM (обращаемся ко всем датчикам) */
    g_sensors.write(0x44); /* CONVERT T (конверсия значения температуры
        и запись в scratchpad) */
}

/***********************************************************************
 *  Задержка на 750мс для получения данных от датчиков
 */
void delay750()
{
    g_indicator.print(SIGN_DP, EMPTY, EMPTY, EMPTY);
    delay(190);
    g_indicator.print(EMPTY, SIGN_DP, EMPTY, EMPTY);
    delay(190);
    g_indicator.print(EMPTY, EMPTY, SIGN_DP, EMPTY);
    delay(190);
    g_indicator.print(EMPTY, EMPTY, EMPTY, SIGN_DP);
    delay(190);
    g_indicator.clear
    ();
}

/***********************************************************************
 *  Чтение данных с датчика
 */
int read_temp(uint8_t num_sensor)
{
    int temp;
    
    /* Читаем значение температуры. Конвертация температуры уже должна
        быть к этому времени выполнена */
    g_sensors.reset();
    g_sensors.select(g_sensors_addr[num_sensor]);
    g_sensors.write(0xBE); /* READ SCRATCHPAD (читаем данные) */

    uint8_t tempL = g_sensors.read(); /* "Нижний" байт данных */
    uint8_t tempH = g_sensors.read(); /* "Верхний" байт данных */

    bool negative = false; /* Флаг отрицательного значения */

    /* Получаем целую часть значения */
    uint16_t ti = (tempH << 8) | tempL;
    if (ti & 0x8000) {
        ti = -ti;
        negative = true;
    }
  
    /* Переводим дробную часть (4 бита) из двоичной системы в десятичную.
        Оставляем только один знак, округляем значение (+8) */
    uint8_t td = ((ti & 0xF) * 10 + 8) / 16;

    /* Формируем значение с фиксированной точкой */
    temp = (ti >> 4) * 10 + td;
    if (negative) temp = -temp;

    return temp;
}

/***********************************************************************
 *  Вывод температуры на индикатор
 */
void show_temp(uint8_t num_sensor)
{
    g_indicator.print_fix(read_temp(num_sensor), 1);
}

/***********************************************************************
 *  Запись в EEPROM
 */
void EEPROM_write(uint16_t addr, uint8_t data)
{
    while (EECR & (1<<EEPE)); /* Ждём завершения предыдущей записи */
    
    EEAR = addr;
    EEDR = data;
    EECR |= (1<<EEMPE); /* Так надо, зачем - не понял */
    EECR |= (1<<EEPE); /* Начинаем запись */
}

/***********************************************************************
 *  Чтение из EEPROM
 */
uint8_t EEPROM_read(uint16_t addr)
{
    while (EECR & (1<<EEPE)); /* Ждём завершения предыдущей записи */
    EEAR = addr;
    EECR |= (1<<EERE);
    return EEDR;
}

/***********************************************************************
 *  Опрос состояния кнопок
 *  p_ctrl_state  - состояние кнопок ([4]-[3]-[2]-[1], 0 - не нажата,
 *      1 - нажата).
 *  Возврат: номер кнопки, требующей обработки (1..4).
 */
uint8_t test_buttons(uint8_t *p_ctrl_state)
{
    uint8_t signaled_button = 0;
    uint8_t pressed_button = 0;

    uint8_t buttons_hard_state = PIND & 0b1111;

    if (buttons_hard_state != g_buttons_hard_state) {
        delay(50); /* Ждём завершения дребезга контактов. Не самое
            лучшее решение, но ограничимся им */
        buttons_hard_state = PIND & 0b1111;
    }

    /* Проверяем все кнопки по очереди */
    for (int i = 0; i < 4; i++) {
        uint8_t mask = (1 << i);

        if ((buttons_hard_state & mask)
                != (g_buttons_hard_state & mask)) {
            /* Была нажата кнопка */
            if ((buttons_hard_state & mask) == 0) {
                g_buttons_ctrl_state |= mask; /* Сохраняем в состоянии
                    контрольных кнопок */
                pressed_button = i + 1; /* Запоминаем номер нажатой
                    кнопки. При одновременном нажатии кнопок приоритет
                    за той, что имеет больший номер */
            }
            /* Была отпущена кнопка */
            else {
                /*  Если отпущена "нажатая" кнопка,
                    сигнализируем об этом */
                if (i == g_pressed_button - 1) {
                    /*  Сохраняем состояние контрольных кнопок на
                        случай, если с "нажатой" кнопкой одновременно
                        были отпущены и контрольные. Если этого не
                        сделать, то для следующих проверяемых кнопок
                        "нажатая" кнопка уже будет отсутствовать и их
                        состояние может быть сброшено */
                    *p_ctrl_state = g_buttons_ctrl_state & ~mask;
                    /*  Приводим состояние контрльных кнопок
                        в соответствие с фактическим положением дел */
                    g_buttons_ctrl_state = ~buttons_hard_state & 0x0F;
                    g_pressed_button = 0;
                    signaled_button = i + 1;
                }
                /*  Если есть "нажатая" кнопка, то отпускание
                    проверяемой кнопки НЕ приводит к исключению её из
                    состояния контрольных кнопок. Т.е. если были нажаты
                    последовательно кнопки [1] и [2], то вне зависимости
                    от порядка их отпускания программой будет выполнена
                    комбинация [1]+[2] (не [2] и не [2]+[1]). Если же
                    "нажатой" кнопки нет (была уже отпущена, или были
                    одновременно нажаты несколько кнопок), то кнопка
                    спокойно из списка контрольных кнопок исключается */
                else if (g_pressed_button == 0) g_buttons_ctrl_state &= ~mask;
            }
        } /* if ((buttons_hard_state & mask)
                     != (g_buttons_hard_state & mask)) */
    } /* for (int i = 0; i < 4; i++) */
  
    /*  Сохраняем номер "нажатой" кнопки, начинаем отсчёт времени
        удержания кнопки */
    if (pressed_button) {
        g_pressed_button = pressed_button;
        g_pressed_timestamp = millis();
        g_pressed_timestamp_first = true;
    }

    g_buttons_hard_state = buttons_hard_state;

    /*  Иммитация многократного нажатия при удержании кнопок более
        1 секунды (но только для тех сочетаний, где это имеет смысл!) */
    if (g_pressed_button &&
            (g_buttons_ctrl_state == 0b0001
            || g_buttons_ctrl_state == 0b0010
            || g_buttons_ctrl_state == 0b0100
            || g_buttons_ctrl_state == 0b1000)) {

        unsigned long timestamp = millis();
        unsigned elapsed = (unsigned)(timestamp - g_pressed_timestamp);
    
        if (g_pressed_timestamp_first && elapsed >= 1000
                || !g_pressed_timestamp_first && elapsed >= 200) {
  
            *p_ctrl_state = g_buttons_ctrl_state & ~(1 << (g_pressed_button - 1));
            signaled_button = g_pressed_button;
            g_pressed_timestamp = timestamp;
            g_pressed_timestamp_first = false;
        }
    }

    return signaled_button;
}

/***********************************************************************
 * Сравнение данных
 */
bool cmp(void *dat1, void *dat2, unsigned int len)
{
    bool res = true;
    
    for (unsigned int i = 0; i < len; i++) {
        if ( ((uint8_t*)dat1)[i] != ((uint8_t*)dat2)[i]) {
            res = false;
            break;
        }
    }
    
    return res;
}

/***********************************************************************
 * Обмен данными
 */
void swap(void *dat1, void *dat2, unsigned int len)
{
    for (unsigned int i = 0; i < len; i++) {
        uint8_t sw = ((uint8_t*)dat1)[i];
        ((uint8_t*)dat1)[i] = ((uint8_t*)dat2)[i];
        ((uint8_t*)dat2)[i] = sw;
    }
}

/***********************************************************************
 * Перемена датчиков местами
 */
void swap_sensors()
{
    swap(g_sensors_addr[0], g_sensors_addr[1], 8);
          
    for (int i = 0; i < 16; i++) {
        EEPROM_write(i + 1, g_sensors_addr[0][i]);
    }
  
    if (g_control_sensor != -1) {
        g_control_sensor = (g_control_sensor == 0 ? 1 : 0);
        EEPROM_write(0, g_control_sensor);
    }

    show_temp(g_mode == MODE_SENSOR1 ? 0 : 1);
}

/***********************************************************************
 * Смена режима
 */
void change_mode(mode_t new_mode)
{
    if (new_mode != g_mode) {
        uint8_t mem[4] = {0};
        anim_t anim_type = ANIM_NO;
        int8_t brightness = 15;

        g_blink_timestamp = millis();
        g_blink_step = 0;

        switch (new_mode) {
        case MODE_SENSOR1:
            indicator_t::memprint_fix(mem, read_temp(0), 1);
            anim_type = ANIM_GOLEFT;
            break;

        case MODE_SENSOR2:
            indicator_t::memprint_fix(mem, read_temp(1), 1);
            anim_type = ANIM_GORIGHT;
            break;

        case MODE_CONTROL:
            indicator_t::memprint_fix(mem, g_control_temp, 1);
            anim_type = ANIM_GOUP;
            brightness = (g_control_actived ? 15 : 4);
            break;

        case MODE_OFF:
            if (g_control_actived) {
                brightness = 15;
                indicator_t::memprint(mem, EMPTY, CHAR_o, CHAR_n, EMPTY);
            }
            else {
                indicator_t::memprint_fix(mem, EMPTY, CHAR_o, CHAR_F, CHAR_F);
                brightness = 4;
            }
            anim_type = ANIM_GODOWN;
            break;
        } /* switch (new_mode) */
    
        if (g_mode == MODE_CONTROL) {
            anim_type = ANIM_GODOWN;
            g_blink_timestamp = millis();
            g_blink_step = 0;
        }
        else if (g_mode == MODE_OFF || g_mode == MODE_ERROR) {
            anim_type = ANIM_GOUP;
            g_blink_timestamp = millis();
            g_blink_step = 0;
        }

        g_indicator.anim(mem, anim_type, 100, brightness);

        g_mode = new_mode;
    } /* if (new_mode != g_mode) */
}

/***********************************************************************
 * Отключение датчиков от контроля температуры
 */
void clear_control_sensor()
{
    g_control_sensor = -1;
    EEPROM_write(0, g_control_sensor);
    g_indicator.print(EMPTY, EMPTY, EMPTY, SIGN_MINUS);
    delay(200);
}

/***********************************************************************
 * Определение датчика с контролем температуры
 */
void set_control_sensor(int8_t control_sensor)
{
    g_control_sensor = control_sensor;
    EEPROM_write(0, g_control_sensor);
    g_indicator.clear();
    delay(200);
}

/***********************************************************************
 * Функция настройки приложения 
 */
void setup()
{
    /*  Важный комментарий из даташита про неиспользуемые пины
     *  (14.2.6 Unconnected Pins):
     *  "Если некоторые пины не используются, рекомендуется убедиться,
     *  что эти выводы имеют определенный уровень. Хотя большинство
     *  цифровых входов отключены в режимах глубокого сна, как описано
     *  выше, плавающие входы следует избегать, чтобы уменьшить
     *  потребление тока во всех других режимах, когда цифровые входы
     *  включены (сброс, Активный режим и режим ожидания). Самый простой
     *  способ, чтобы гарантировать определенный уровень
     *  у неиспользуемого пина - включение внутреннего подтягивающего
     *  резистора. В этом случае подтягивающие резисторы будут отключены
     *  во время сброса. Если важно низкое энергопотребление в режиме
     *  сброса, рекомендуется использовать внешний подтягивающий
     *  резистор к плюсу или к минусу. Подключение неиспользуемых
     *  выводов непосредственно к VCC или GND не рекомендуется,
     *  поскольку это может привести к чрезмерным токам, если вывод
     *  случайно будет сконфигурирован как выход".
     */

  
    /***
     * Настраиваем порты
     */

    /*  Порт B (аноды индикатора) не трогаем, настраивается отдельно */
    
    /*  Порт C:
     *  C7 - отсутствует
     *  C6 - не используется (input, pull-up)
     *  C5-C2 - катоды индикаторы (не трогаем, настраивается отдельно)
     *  C1-C0 - не используются (input, pull-up)
     */
    DDRC  =  (DDRC & 0b00111100) | 0b00000000;
    PORTC = (PORTC & 0b00111100) | 0b01000011;

    /*  Порт D:
     *  D7 - температурные датчики (не трогаем, настраивается отдельно)
     *  D6 - пищалка (output, low)
     *  D5 - не используется (input, pull-up)
     *  D4 - пин управления реле (output, low)
     *  D3-D0 - кнопки (input, pull-up)
     */
    DDRD  =  (DDRD & 0b10000000) | 0b01010000;
    PORTD = (PORTD & 0b10000000) | 0b00101111;
  
    /* Устанавливаем прерывания на нажатия кнопок */
    PCICR = (1 << PCIE2);
    PCMSK2 = (1 << PCINT16) | (1 << PCINT17) | (1 << PCINT18) | (1 << PCINT19);


    /***
     * Разбираемся с датчиками 
     */

    /* Ищем датчики */
    if (!g_sensors.search(g_sensors_addr[0])
            || !g_sensors.search(g_sensors_addr[1])) {
        g_mode = MODE_ERROR;
        g_errno = 2; /* Нет датчика(-ов) (возможно, не подключен
            второй блок) */
    }
    else {
        /* Загружаем номер датчика с контролем температуры */
        g_control_sensor = EEPROM_read(0); /* До записи == -1 */

        /*  Загружаем из EEPROM идентификаторы датчиков для сравнения
            и установки порядка */
        uint8_t addr[2][8];
        for (int i = 0; i < 16; i++)
            addr[0][i] = EEPROM_read(i + 1);

        if (cmp(addr[0], g_sensors_addr[0], 8)
                && cmp(addr[1], g_sensors_addr[1], 8)) {
            /*  Идентификаторы датчиков соответствуют сохранённым.
                Порядок - нормальный */
        }
        else if (cmp(addr[0], g_sensors_addr[1], 8)
                && cmp(addr[1], g_sensors_addr[0], 8)) {
            /*  Идентификаторы датчиков соответствуют сохранённым.
                Меняем порядок */
            swap(g_sensors_addr[0], g_sensors_addr[1], 8);
        }
        else {
            g_mode = MODE_ERROR;
            g_errno = 1; /* Идентификаторы реальных датчиков не
                соответствуют сохранённым ранее (возможно, блоки от
                разных устройств, или устройство запускается впервые.
                Требуется инициализация) */

            /* Сохраняем идентификаторы найденных датчиков */
            for (int i = 0; i < 16; i++)
                EEPROM_write(i + 1, g_sensors_addr[0][i]);
        }
    } /* else -> if (!g_sensors.search(g_sensors_addr[0])
                         || !g_sensors.search(g_sensors_addr[1])) */


    /* Загружаем последнюю используемую температуру для контроля */
    uint8_t tl = EEPROM_read(17);
    uint8_t th = EEPROM_read(18);
    if (tl != 0xFF || th != 0xFF)
        g_control_temp = tl | (th << 8);

    /* Ждём первых результатов от датчиков */
    convertT();
    g_poll_timestamp = millis();
    delay750();
}


/***********************************************************************
 * Основной цикл
 */
void loop()
{
    uint8_t ctrl_state = 0;
    uint8_t signaled_button = test_buttons(&ctrl_state);
    bool update = false;
  
    /* Опрос датчиков каждую секунду */
    if (millis() - g_poll_timestamp > 1000) {
        update = true;
        convertT();
        g_poll_timestamp = millis();
    }

    /*  Обработка сигнала (отпускания) кнопки.
     *  [1] - отмена контроля температуры
     *  [2] - запуск контроля температуры
     *  [3] – 1-й датчик
     *  [4] – 2-й датчик
     *  [3]+[4] – настройки: перемена датчиков местами
     *  [2]+[3] - настройки: 1-й датчик с контролем температуры
     *  [2]+[4] - настройки: 2-й датчик с контролем температуры
     *  [2]+[3]+[4] - настройки: нет датчиков с контролем температуры
     */
    if (signaled_button) {
        if (g_mode == MODE_ERROR
                || g_mode == MODE_SENSOR1 || g_mode == MODE_SENSOR2)
        switch (signaled_button) {
        case 1:
            if (ctrl_state == 0) {
                if (g_control_sensor == -1) {
                    g_indicator.print(EMPTY, EMPTY, CHAR_n, CHAR_o);
//                    g_indicator.set_brightness( g_indicator.get_brightness() - 1 );
//                    g_indicator.print_int(g_indicator.get_brightness(), DIG1, DIG4, DIGIT_0);
                    delay(500);
                }
                else {
                    change_mode(g_control_sensor == 0 ? MODE_SENSOR1 : MODE_SENSOR2);
                    delay(300);
                    change_mode(MODE_OFF);
                    g_mode_control_timestamp = millis();
                }
            }
            break;
        
        case 2:
            if (ctrl_state == 0) {
                if (g_control_sensor == -1) {
                    g_indicator.print(EMPTY, EMPTY, CHAR_n, CHAR_o);
//                    g_indicator.set_brightness( g_indicator.get_brightness() + 1 );
//                    g_indicator.print_int(g_indicator.get_brightness(), DIG1, DIG4, DIGIT_0);
                    delay(500);
                }
                else {
                    change_mode(g_control_sensor == 0 ? MODE_SENSOR1 : MODE_SENSOR2);
                    delay(300);
                    change_mode(MODE_CONTROL);
                    g_mode_control_timestamp = millis();
                }
            }
            break;

        case 3:
            if (ctrl_state == 0) change_mode(MODE_SENSOR1);
            /* [4]+[3] - меняем датчики местами */
            else if (ctrl_state == 0b1000) swap_sensors();
            /*  [2]+[3] - устанавливаем контроль температуры
                на первый датчик */
            else if (ctrl_state == 0b0010) {
                change_mode(MODE_SENSOR1);
                set_control_sensor(0);
            }
            /* [2]+[4]+[3] - отключаем контроль температуры */
            else if (ctrl_state == 0b1010) clear_control_sensor();
            break;

        case 4:
            if (ctrl_state == 0) change_mode(MODE_SENSOR2);
            /* [3]+[4] - меняем датчики местами  */
            else if (ctrl_state == 0b0100) swap_sensors();
            /* [2]+[4] - устанавливаем контроль температуры
                на второй датчик */
            else if (ctrl_state == 0b0010) {
                change_mode(MODE_SENSOR2);
                set_control_sensor(1);
            }
            /* [2]+[3]+[4] - отключаем контроль температуры */
            else if (ctrl_state == 0b0110) clear_control_sensor();
            break;        
        } /* if (g_mode == MODE_ERROR
                     || g_mode == MODE_SENSOR1 || g_mode == MODE_SENSOR2)
             switch (signaled_button) */

        else if (g_mode == MODE_CONTROL) {
            g_mode_control_timestamp = millis();

            if (ctrl_state == 0) {
                update = true;
        
                if (!g_control_actived) {
                    g_control_actived = true;
                    g_blink_timestamp = millis();
                    g_blink_step = 0;
                }
                else {
                    switch (signaled_button) {
                        case 1: g_control_temp -= 50; break;
                        case 2: g_control_temp -= 10; break;
                        case 3: g_control_temp += 10; break;
                        case 4: g_control_temp += 50; break;
                    }
                    EEPROM_write(17, g_control_temp & 0xFF);
                    EEPROM_write(18, g_control_temp >> 8);
                }
            }
        } /* else if (g_mode == MODE_CONTROL) */

        else if (g_mode == MODE_OFF) {
            if (ctrl_state == 0 && signaled_button == 1) {
                g_control_actived = false;
                g_mode_control_timestamp = millis();
                HEATER_OFF();
                update = true;
            }
        }
    } /* if (signaled_button) */

    switch (g_mode) {
    case MODE_ERROR: {
        uint8_t buf[4];
        indicator_t::memprint_int(buf, g_errno);
        indicator_t::memprint(buf, CHAR_E, g_errno < 10 ? DIG3 : DIG2);
//        indicator_t::memprint_int(buf, g_errno, DIG2, DIG4, DIGIT_0);
//        indicator_t::memprint(buf, CHAR_E, DIG1);
        g_indicator.print(buf);
    }   break;
    
    case MODE_SENSOR1:
    case MODE_SENSOR2: {
        int8_t sensor = (g_mode == MODE_SENSOR1 ? 0 : 1);
        if (update) show_temp(sensor);
    
        if (g_control_actived) {
            if (millis() - g_blink_timestamp
                    > (g_blink_step == 0 ? 1000 : 20)) {
                g_blink_timestamp = millis();

                if (++g_blink_step >= 30) g_blink_step = 0;

                if (g_blink_step <= 15)
                    g_indicator.set_brightness(15 - g_blink_step);
                else
                    g_indicator.set_brightness(g_blink_step - 15);
            }
        } /* if (g_control_actived) */
    } break;

    case MODE_CONTROL:
        if (millis() - g_mode_control_timestamp > 3000) {
            change_mode(g_control_sensor == 0
                ? MODE_SENSOR1 : MODE_SENSOR2);
        }
        else if (update) {
            g_indicator.set_brightness(g_control_actived ? 15 : 4);
            g_indicator.print_fix(g_control_temp, 1);
        }
        break;  

    case MODE_OFF:
        if (millis() - g_mode_control_timestamp > 3000) {
            change_mode(g_control_sensor == 0
                ? MODE_SENSOR1 : MODE_SENSOR2);
        }
        else if (update) {
            if (g_control_actived) {
                g_indicator.set_brightness(15);
                g_indicator.print(EMPTY, CHAR_o, CHAR_n, EMPTY);
            }
            else {
                g_indicator.set_brightness(4);
                g_indicator.print(EMPTY, CHAR_o, CHAR_F, CHAR_F);
            }
        }
        break;
    }

    if (g_control_actived) {
        if (read_temp(g_control_sensor) < g_control_temp) HEATER_ON();
        else HEATER_OFF();
    }

    /* Засыпаем в свободное время (TIMER2 используется для индикации, TIMER0 для расчёта millis() */
    LowPower.idle(SLEEP_FOREVER, ADC_OFF, TIMER2_ON, TIMER1_OFF, TIMER0_ON, SPI_OFF, USART0_OFF, TWI_OFF);
}

