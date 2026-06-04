#include "faculty175_face_tarot_assets.h"

#include <stddef.h>

static const faculty175_tarot_card_t k_cards[FACULTY175_TAROT_CARD_COUNT] = {
    {"The Fool", "0", "BEGIN", "fool", 244, 204, 90},
    {"The Magician", "I", "WILL", "magician", 220, 70, 64},
    {"High Priestess", "II", "VEIL", "priestess", 78, 116, 210},
    {"The Empress", "III", "BLOOM", "empress", 88, 170, 98},
    {"The Emperor", "IV", "ORDER", "emperor", 196, 82, 54},
    {"The Hierophant", "V", "RITE", "hierophant", 190, 170, 108},
    {"The Lovers", "VI", "CHOICE", "lovers", 225, 112, 142},
    {"The Chariot", "VII", "DRIVE", "chariot", 80, 132, 210},
    {"Strength", "VIII", "GENTLE", "strength", 238, 170, 76},
    {"The Hermit", "IX", "LAMP", "hermit", 170, 186, 205},
    {"Wheel Fortune", "X", "TURN", "fortune", 214, 174, 72},
    {"Justice", "XI", "BALANCE", "justice", 190, 82, 86},
    {"Hanged Man", "XII", "PAUSE", "hanged", 92, 166, 190},
    {"Death", "XIII", "CHANGE", "death", 210, 210, 210},
    {"Temperance", "XIV", "BLEND", "temperance", 116, 184, 164},
    {"The Devil", "XV", "CHAIN", "devil", 174, 64, 72},
    {"The Tower", "XVI", "BREAK", "tower", 230, 144, 64},
    {"The Star", "XVII", "HOPE", "star", 116, 174, 226},
    {"The Moon", "XVIII", "DREAM", "moon", 150, 150, 218},
    {"The Sun", "XIX", "JOY", "sun", 248, 204, 74},
    {"Judgement", "XX", "CALL", "judgement", 214, 130, 92},
    {"The World", "XXI", "WHOLE", "world", 116, 190, 142},
};

const faculty175_tarot_card_t *faculty175_tarot_card_get(int idx)
{
    if (idx < 0 || idx >= FACULTY175_TAROT_CARD_COUNT) {
        return NULL;
    }
    return &k_cards[idx];
}
