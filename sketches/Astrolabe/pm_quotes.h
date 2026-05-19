#pragma once

#include <stddef.h>
#include <stdint.h>

struct PmQuoteOfDay {
  bool ok = false;
  bool demo = false;
  char date[16];
  int index = 0;
  int total = 0;
  char faculty_slug[32];
  char faculty_name[36];
  char quote[240];
  char passage[64];
  char book_title[64];
  char book_author[48];
  char error[64];
};

bool pm_quotes_fetch(PmQuoteOfDay *out);
void pm_quotes_fill_demo(PmQuoteOfDay *out);

