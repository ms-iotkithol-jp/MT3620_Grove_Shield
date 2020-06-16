#include "GroveTempHumiBaroBME280.h"
#include <stdlib.h>
#include <math.h>
#include "../HAL/GroveI2C.h"

#define BME280_ADDRESS				(0x76 << 1)

#define BME280_REG_DIG_T1			(0x88)
#define BME280_REG_DIG_T2			(0x8A)
#define BME280_REG_DIG_T3			(0x8C)
#define BME280_REG_CHIPID			(0xD0)
#define BME280_REG_CONTROLHUMID		(0xF2)
#define BME280_REG_CONTROL			(0xF4)
#define BME280_REG_TEMPDATA			(0xFA)

#define BME280_REG_DIG_P1    (0x8E)
#define BME280_REG_DIG_P2    (0x90)
#define BME280_REG_DIG_P3    (0x92)
#define BME280_REG_DIG_P4    (0x94)
#define BME280_REG_DIG_P5    (0x96)
#define BME280_REG_DIG_P6    (0x98)
#define BME280_REG_DIG_P7    (0x9A)
#define BME280_REG_DIG_P8    (0x9C)
#define BME280_REG_DIG_P9    (0x9E)
#define BME280_REG_PRESSUREDATA    (0xF7)

#define BME280_REG_DIG_H1    (0xA1)
#define BME280_REG_DIG_H2    (0xE1)
#define BME280_REG_DIG_H3    (0xE3)
#define BME280_REG_DIG_H4    (0xE4)
#define BME280_REG_DIG_H5    (0xE5)
#define BME280_REG_DIG_H6    (0xE7)
#define BME280_REG_HUMIDITYDATA    (0xFD)


typedef struct
{
	int I2cFd;
	float Temperature;
	float Humidity;
	float Pressure;

	// Calibration data
	uint16_t dig_T1;
	int16_t dig_T2;
	int16_t dig_T3;
	uint16_t dig_P1;
	int16_t dig_P2;
	int16_t dig_P3;
	int16_t dig_P4;
	int16_t dig_P5;
	int16_t dig_P6;
	int16_t dig_P7;
	int16_t dig_P8;
	int16_t dig_P9;
	uint8_t dig_H1;
	int16_t dig_H2;
	uint8_t dig_H3;
	int16_t dig_H4;
	int16_t dig_H5;
	int8_t  dig_H6;
	int32_t t_fine;
}
GroveTempHumiBaroBME280Instance;

void* GroveTempHumiBaroBME280_Open(int i2cFd)
{
	GroveTempHumiBaroBME280Instance* this = (GroveTempHumiBaroBME280Instance*)malloc(sizeof(GroveTempHumiBaroBME280Instance));

	this->I2cFd = i2cFd;
	this->Temperature = NAN;
	this->Humidity = NAN;
	this->Pressure = NAN;


	uint8_t val8;
	if (!GroveI2C_ReadReg8(this->I2cFd, BME280_ADDRESS, BME280_REG_CHIPID, &val8)) return NULL;
	if (val8 != 0x60) return NULL;

	bool result = GroveI2C_ReadReg16(this->I2cFd,BME280_ADDRESS, BME280_REG_DIG_T1, &this->dig_T1);
	result = GroveI2C_ReadReg16(this->I2cFd, BME280_ADDRESS, BME280_REG_DIG_T2, &this->dig_T2);
	result = GroveI2C_ReadReg16(this->I2cFd, BME280_ADDRESS, BME280_REG_DIG_T3, &this->dig_T3);

	result = GroveI2C_ReadReg16(this->I2cFd, BME280_ADDRESS, BME280_REG_DIG_P1, &this->dig_P1);
	result = GroveI2C_ReadReg16(this->I2cFd, BME280_ADDRESS, BME280_REG_DIG_P2, &this->dig_P2);
	result = GroveI2C_ReadReg16(this->I2cFd, BME280_ADDRESS, BME280_REG_DIG_P3, &this->dig_P3);
	result = GroveI2C_ReadReg16(this->I2cFd, BME280_ADDRESS, BME280_REG_DIG_P4, &this->dig_P4);
	result = GroveI2C_ReadReg16(this->I2cFd, BME280_ADDRESS, BME280_REG_DIG_P5, &this->dig_P5);
	result = GroveI2C_ReadReg16(this->I2cFd, BME280_ADDRESS, BME280_REG_DIG_P6, &this->dig_P6);
	result = GroveI2C_ReadReg16(this->I2cFd, BME280_ADDRESS, BME280_REG_DIG_P7, &this->dig_P7);
	result = GroveI2C_ReadReg16(this->I2cFd, BME280_ADDRESS, BME280_REG_DIG_P8, &this->dig_P8);
	result = GroveI2C_ReadReg16(this->I2cFd, BME280_ADDRESS, BME280_REG_DIG_P9, &this->dig_P9);

	result = GroveI2C_ReadReg8(this->I2cFd, BME280_ADDRESS, BME280_REG_DIG_H1, &this->dig_H1);
	result = GroveI2C_ReadReg16(this->I2cFd, BME280_ADDRESS, BME280_REG_DIG_H2, &this->dig_H2);
	result = GroveI2C_ReadReg8(this->I2cFd, BME280_ADDRESS, BME280_REG_DIG_H3, &this->dig_H3);

	int8_t temp8h, temp8l;
	if ((result = (GroveI2C_ReadReg8(this->I2cFd, BME280_ADDRESS, BME280_REG_DIG_H4, &temp8h) && GroveI2C_ReadReg8(this->I2cFd, BME280_ADDRESS, BME280_REG_DIG_H4 + 1, &temp8l))))
	{
		this->dig_H4 = (int16_t)((temp8h << 4) | (0x0F & temp8l));
	}
	if ((result = (GroveI2C_ReadReg8(this->I2cFd, BME280_ADDRESS, BME280_REG_DIG_H5 + 1, &temp8h) && GroveI2C_ReadReg8(this->I2cFd, BME280_ADDRESS, BME280_REG_DIG_H5, &temp8l))))
	{
		this->dig_H5 = (int16_t)((temp8h << 4) | (0x0F & temp8l >> 4));
	}
	result = GroveI2C_ReadReg8(this->I2cFd, BME280_ADDRESS, BME280_REG_DIG_H6, &this->dig_H6);


	GroveI2C_WriteReg8(this->I2cFd, BME280_ADDRESS, BME280_REG_CONTROLHUMID, 0x05);
	GroveI2C_WriteReg8(this->I2cFd, BME280_ADDRESS, BME280_REG_CONTROL, 0xb7);

	return this;
}

void GroveTempHumiBaroBME280_Read(void* inst)
{
	GroveTempHumiBaroBME280Instance* this = (GroveTempHumiBaroBME280Instance*)inst;

	this->Temperature = NAN;

	uint16_t dig_T1;
	int16_t dig_T2;
	int16_t dig_T3;
	if (!GroveI2C_ReadReg16(this->I2cFd, BME280_ADDRESS, BME280_REG_DIG_T1, &dig_T1)) return;
	if (!GroveI2C_ReadReg16(this->I2cFd, BME280_ADDRESS, BME280_REG_DIG_T2, (uint16_t*)&dig_T2)) return;
	if (!GroveI2C_ReadReg16(this->I2cFd, BME280_ADDRESS, BME280_REG_DIG_T3, (uint16_t*)&dig_T3)) return;

	int32_t adc_T;
	if (!GroveI2C_ReadReg24BE(this->I2cFd, BME280_ADDRESS, BME280_REG_TEMPDATA, &adc_T)) return;

	adc_T >>= 4;
	int32_t var1 = (((adc_T >> 3) - ((int32_t)(dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
	int32_t var2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) * ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) * ((int32_t)dig_T3)) >> 14;

	int32_t t_fine = var1 + var2;
	this->Temperature = (float)((t_fine * 5 + 128) >> 8) / 100;

	// Read Pressure
	int32_t adc_P;
	this->Pressure = NAN;
	if (!GroveI2C_ReadReg24BE(this->I2cFd, BME280_ADDRESS, BME280_REG_PRESSUREDATA, &adc_P)) return;
	adc_P >>= 4;

	int64_t var64_1, var64_2, p;
	var64_1 = ((int64_t)t_fine) - 128000;
	var64_2 = var64_1 * var64_1 * (int64_t)this->dig_P6;
	var64_2 = var64_2 + ((var64_1 * (int64_t)this->dig_P5) << 17);
	var64_2 = var64_2 + (((int64_t)this->dig_P4) << 35);
	var64_1 = ((var64_1 * var64_1 * (int64_t)this->dig_P3) >> 8) + ((var64_1 * (int64_t)this->dig_P2) << 12);
	var64_1 = (((((int64_t)1) << 47) + var64_1)) * ((int64_t)this->dig_P1) >> 33;
	if (var64_1 == 0) {
		return; // avoid exception caused by division by zero
	}
	p = 1048576 - adc_P;
	p = (((p << 31) - var64_2) * 3125) / var64_1;
	var64_1 = (((int64_t)this->dig_P9) * (p >> 13) * (p >> 13)) >> 25;
	var64_2 = (((int64_t)this->dig_P8) * p) >> 19;
	p = ((p + var64_1 + var64_2) >> 8) + (((int64_t)this->dig_P7) << 4);
	this->Pressure = (float)p / (float)256;

	// For Humidity
	this->Humidity = NAN;
	int32_t adc_H, v_x1_u32r;
	uint16_t adc_H16;
	if (!GroveI2C_ReadReg16(this->I2cFd, BME280_ADDRESS, BME280_REG_HUMIDITYDATA, &adc_H16)) return;
	adc_H = adc_H16;
	v_x1_u32r = (t_fine - ((int32_t)76800));
	v_x1_u32r = (((((adc_H << 14) - (((int32_t)this->dig_H4) << 20) - (((int32_t)this->dig_H5) * v_x1_u32r)) + ((
		int32_t)16384)) >> 15) * (((((((v_x1_u32r * ((int32_t)this->dig_H6)) >> 10) * (((v_x1_u32r * ((int32_t)this->dig_H3)) >> 11) + ((
			int32_t)32768))) >> 10) + ((int32_t)2097152)) * ((int32_t)this->dig_H2) + 8192) >> 14));
	v_x1_u32r = (v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) * ((int32_t)this->dig_H1)) >> 4));
	v_x1_u32r = (v_x1_u32r < 0 ? 0 : v_x1_u32r);
	v_x1_u32r = (v_x1_u32r > 419430400 ? 419430400 : v_x1_u32r);
	this->Humidity = (float)((uint32_t)(v_x1_u32r >> 12)) / (float)1024;

}

float GroveTempHumiBaroBME280_GetTemperature(void* inst)
{
	GroveTempHumiBaroBME280Instance* this = (GroveTempHumiBaroBME280Instance*)inst;

	return this->Temperature;
}

float GroveTempHumiBaroBME280_GetHumidity(void* inst)
{
	GroveTempHumiBaroBME280Instance* this = (GroveTempHumiBaroBME280Instance*)inst;

	return this->Humidity;
}

float GroveTempHumiBaroBME280_GetPressure(void* inst)
{
	GroveTempHumiBaroBME280Instance* this = (GroveTempHumiBaroBME280Instance*)inst;

	return (float)this->Pressure;
}

float GroveTempHumiBaroBME280_CalcArtitude(float pressure)
{
	float A = pressure / 101325;
	float B = 1 / 5.25588;
	float C = pow(A, B);
	C = 1.0 - C;
	C = C / 0.0000225577;
	return C;
}