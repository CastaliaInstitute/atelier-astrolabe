#pragma once

#include <stdint.h>

/**
 * Parse a `qa …` subcommand (without the leading `qa `).
 * Returns true if recognized (including errors printed to Serial).
 */
bool pm_qa_inject_command(const char *args);

/**
 * Returns true once for a BOOT event produced by `qa inject boot`.
 *
 * Functional tests use the QA button path to validate routing without starting
 * cloud voice work that can starve the screen server on small internal heaps.
 */
bool pm_qa_consume_injected_boot(void);

/**
 * Returns true once for each gesture produced by `qa inject ...`.
 */
bool pm_qa_consume_injected_gesture(void);
