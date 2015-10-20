#ifndef TERMOCONTROL_H
#define TERMOCONTROL_H

/* Режим работы */
enum mode_t {
    SENSOR1 = 0,
    SENSOR2 = 1,
    MESSAGE,
    SETCONTROL,
    ONOFF,
    NOTHING = 255
};

#endif /* TERMOCONTROL_H */

