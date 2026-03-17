#pragma once
#include <string.h>
#include <stdint.h>

class CSizeBuf
{
public:
	CSizeBuf(uint8_t *buf, uint32_t maxSize)
	{
		m_Data = buf;
		m_MaxSize = maxSize;
		m_CurSize = 0;
		m_bOverflowed = false;
	}

	void WriteByte(uint8_t val)
	{
		if (m_CurSize + 1 > m_MaxSize) { m_bOverflowed = true; return; }
		m_Data[m_CurSize++] = val;
	}

	void WriteChar(char val)
	{
		WriteByte((uint8_t)val);
	}

	void WriteWord(uint16_t val)
	{
		if (m_CurSize + 2 > m_MaxSize) { m_bOverflowed = true; return; }
		m_Data[m_CurSize++] = (uint8_t)(val & 0xFF);
		m_Data[m_CurSize++] = (uint8_t)(val >> 8);
	}

	void WriteDWord(uint32_t val)
	{
		if (m_CurSize + 4 > m_MaxSize) { m_bOverflowed = true; return; }
		m_Data[m_CurSize++] = (uint8_t)(val & 0xFF);
		m_Data[m_CurSize++] = (uint8_t)((val >> 8) & 0xFF);
		m_Data[m_CurSize++] = (uint8_t)((val >> 16) & 0xFF);
		m_Data[m_CurSize++] = (uint8_t)((val >> 24) & 0xFF);
	}

	void WriteString(const char *str, bool writeNull = true)
	{
		if (!str) { if (writeNull) WriteByte(0); return; }
		int len = (int)strlen(str);
		if (m_CurSize + len + (writeNull ? 1 : 0) > m_MaxSize) { m_bOverflowed = true; return; }
		memcpy(m_Data + m_CurSize, str, len);
		m_CurSize += len;
		if (writeNull) m_Data[m_CurSize++] = 0;
	}

	uint32_t GetCurSize() const { return m_CurSize; }
	bool IsOverflowed() const { return m_bOverflowed; }

private:
	uint8_t *m_Data;
	uint32_t m_MaxSize;
	uint32_t m_CurSize;
	bool m_bOverflowed;
};
