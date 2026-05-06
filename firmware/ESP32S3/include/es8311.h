#pragma once
#include <stdbool.h>

/**
 * es8311.h - Driver I2C danh thuc chip am thanh ES8311
 * Board: Xiaozhi S3 (ES3N28P)
 */

/** Khoi tao ES8311 qua I2C. Goi Wire.begin() truoc. Tra ve true = thanh cong. */
bool es8311_init();

/** Kiem tra nhanh chip con song tren I2C. true = OK, false = mat ket noi. */
bool es8311_check();

