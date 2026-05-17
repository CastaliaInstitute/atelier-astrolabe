#pragma once

#include <stdint.h>

/**
 * Parse a `qa …` subcommand (without the leading `qa `).
 * Returns true if recognized (including errors printed to Serial).
 */
bool pm_qa_inject_command(const char *args);
