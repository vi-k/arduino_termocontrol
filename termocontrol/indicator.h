#ifndef INDICATOR_H
#define INDICATOR_H

#include <stddef.h>

/* Изображения цифр, букв и знаков для индикатора
 *      6
 *    7   0
 *      1
 *    5   2   
 *      4   3
 */
#define EMPTY       0
#define DIGIT_0     0b11110101  /* 0 */
#define DIGIT_1     0b00000101  /* 1 */
#define DIGIT_2     0b01110011  /* 2 */
#define DIGIT_3     0b01010111  /* 3 */
#define DIGIT_4     0b10000111  /* 4 */
#define DIGIT_5     0b11010110  /* 5 */
#define DIGIT_6     0b11110110  /* 6 */
#define DIGIT_7     0b01000101  /* 7 */
#define DIGIT_8     0b11110111  /* 8 */
#define DIGIT_9     0b11010111  /* 9 */
#define SIGN_MINUS  0b00000010  /* - */
#define SIGN_DP     0b00001000  /* . */
#define CHAR_A      0b11100111  /* A */
#define CHAR_b      0b10110110  /* b */
#define CHAR_C      0b11110000  /* C */
#define CHAR_c      0b00110010  /* c */
#define CHAR_d      0b00110111  /* d */
#define CHAR_E      0b11110010  /* E */
#define CHAR_F      0b11100010  /* F */
#define CHAR_G      0b11110100  /* G */
#define CHAR_h      0b10100110  /* h */
#define CHAR_I      0b10100000  /* I(слева) */
#define CHAR_IR     DIGIT_1     /* I (справа) */
#define CHAR_i      0b00100000  /* i(слева) */
#define CHAR_iR     0b00000100  /* i(справа) */
#define CHAR_J      0b00010101  /* J */
#define CHAR_L      0b10110000  /* L */
#define CHAR_n      0b00100110  /* n */
#define CHAR_O      DIGIT_0     /* O */
#define CHAR_o      0b00110110  /* o */
#define CHAR_P      0b11100011  /* P */
#define CHAR_r      0b00100010  /* r */
#define CHAR_S      DIGIT_5     /* S */
#define CHAR_t      0b10110010  /* t */
#define CHAR_U      0b10110101  /* U */
#define CHAR_u      0b00110100  /* u */
#define CHAR_Y      0b10010111  /* Y */
#define CHAR_Z      DIGIT_2     /* Z */
#define SIGN_QUOT   0b10000001  /* " */
#define SIGN_APOL   0b10000000  /* '(слева) */
#define SIGN_APOR   0b00000001  /* '(справа) */
#define SIGN_LOW    0b00010000  /* _ */
#define SIGN_HIGH   0b01000000  /* ¯ */

/* Номера знакомест на индикаторе */
#define DIG1 1
#define DIG2 2
#define DIG3 3
#define DIG4 4

/* Типы анимации */
enum anim_t
{
  ANIM_NO,
  ANIM_GOLEFT,
  ANIM_GORIGHT,
  ANIM_GOUP,
  ANIM_GODOWN
};

/***********************************************************************
 * Класс индикатора
 */
class indicator_t
{
private:   
    uint8_t digits_[4] = {0}; /* Значения на индикаторе */
    uint8_t digits_n_ = 0; /* Счётчик для динамической индикации */
    uint8_t repeat_counter_ = 1; /* Счётчик для динамической индикации */

    /* Выбор режима индикации заметно влияет на энергопотребление.
     *  Разница между максимальным и предыдущим режимами по
     *  энергозатратам - почти в три раза.
     */
    uint8_t brightness_;

public:        
    indicator_t();
    
    void timer_processing();

    /***
     * Яркость
     */
    void set_brightness(int8_t brightness);
    int8_t get_brightness()
    {
        return brightness_;
    }

    void clear()
    {
        digits_[0] = digits_[1] = digits_[2] = digits_[3] = 0;

        /* Отключаем сразу, не ждём, когда запустится таймер */
        PORTB = 0; /* Аноды на землю */
        PORTC |= 0b00111100; /* Катоды к питанию */
    }
    
    void memclear(uint8_t *mem)
    {
        mem[0] = mem[1] = mem[2] = mem[3] = 0;
    }

    /***
     * Вывод значений в буфер и на индикатор
     */
    static void memprint(
        uint8_t *mem,
        uint8_t d1, uint8_t d2, uint8_t d3, uint8_t d4)
    {
        mem[0] = d1;
        mem[1] = d2;
        mem[2] = d3;
        mem[3] = d4;
    }
        
    void print(
        uint8_t d1, uint8_t d2, uint8_t d3, uint8_t d4)
    {
        digits_[0] = d1;
        digits_[1] = d2;
        digits_[2] = d3;
        digits_[3] = d4;
    }
    
    void print(const uint8_t *mem)
    {
        digits_[0] = mem[0];
        digits_[1] = mem[1];
        digits_[2] = mem[2];
        digits_[3] = mem[3];
    }
            
    static void memprint(uint8_t *mem, uint8_t d, uint8_t dig_n)
    {
        if (dig_n >= DIG1 && dig_n <= DIG4) mem[dig_n - 1] = d;
    }
    
    void print(uint8_t d, uint8_t dig_n)
    {
        if (dig_n >= DIG1 && dig_n <= DIG4) digits_[dig_n - 1] = d;
    }

    static bool memprint_fix(
        uint8_t *mem, int num, uint8_t decimals,
        uint8_t dig_first = DIG1, uint8_t dig_last = DIG4,
        uint8_t space = EMPTY);
    
    bool print_fix(
        int num, uint8_t decimals,
        uint8_t dig_first = DIG1, uint8_t dig_last = DIG4,
        uint8_t space = EMPTY)
    {
        return memprint_fix(
            digits_, num, decimals, dig_first, dig_last, space);
    }
    
    static bool memprint_int(
        uint8_t *mem, int num,
        uint8_t dig_first = DIG1, uint8_t dig_last = DIG4,
        uint8_t space = EMPTY)
    {
        return memprint_fix(mem, num, 0, dig_first, dig_last, space);
    }
        
    bool print_int(
        int num,
        uint8_t dig_first = DIG1, uint8_t dig_last = DIG4,
        uint8_t space = EMPTY)
    {
        return memprint_fix(digits_, num, 0, dig_first, dig_last, space);
    }

    static uint8_t anim_send_up(uint8_t d);
    static uint8_t anim_take_from_bottom(uint8_t d, uint8_t step);
    static uint8_t anim_send_down(uint8_t d);
    static uint8_t anim_take_from_above(uint8_t d, uint8_t step);
    void anim(
        uint8_t *mem, anim_t anim_type, uint16_t step_delay,
        int8_t brightness = -1);
};

#endif /* INDICATOR_H */

