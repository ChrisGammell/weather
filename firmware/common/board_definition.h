#ifndef FK_BOARD_DEFINITION_H_INCLUDED
#define FK_BOARD_DEFINITION_H_INCLUDED

#include "board.h"

namespace fk {

constexpr uint8_t FK_WEATHER_PIN_FLASH_CS = 5;
constexpr uint8_t FK_WEATHER_PIN_PERIPH_ENABLE = 8;

extern Board<BoardConfig<1, 1>> board;

}

#endif
