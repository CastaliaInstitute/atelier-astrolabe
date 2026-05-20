#pragma once

#include <stdint.h>

/**
 * Parse `qa bowl …` subcommands (args after "bowl ").
 * Sets *repaint_out when the bowl face display should refresh.
 * Returns true if recognized (including usage errors printed to Serial).
 */
bool pm_qa_bowl_command(const char *args, bool *repaint_out);
