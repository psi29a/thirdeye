/*
Copyright (C) 2000, 2001  The Exult Team
Copyright (C) 2013 Bret Curtis

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

// Datasource class for universal methods to retreive data from different sources

#ifndef DATABUF_H
#define DATABUF_H

#include <stdio.h>
#include <string.h>

typedef char * charptr;

class DataSource
{
public:
	DataSource() {};
	virtual ~DataSource() {};

	virtual unsigned int read1() =0;
	virtual unsigned int read2() =0;
	virtual unsigned int read2high() =0;
	virtual unsigned int read4() =0;
	virtual unsigned int read4high() =0;
	virtual void read(char *, int) =0;

	virtual void write1(unsigned int) =0;
	virtual void write2(unsigned int) =0;
	virtual void write2high(unsigned int) =0;
	virtual void write4(unsigned int) =0;
	virtual void write4high(unsigned int) =0;

	virtual void seek(unsigned int) =0;
	virtual void skip(int) =0;
	virtual unsigned int getSize() =0;
	virtual unsigned int getPos() =0;
};


class FileDataSource: public DataSource
{
private:
	FILE *f;

	// fgetc returns int; -1 (EOF) becomes 0xff after a byte cast and silently
	// corrupts the parsed value. Treat EOF as 0 so callers see a clean zero
	// instead of a sentinel that looks like a real byte.
	unsigned int getbyte()
	{
		int c = fgetc(f);
		return (c == EOF) ? 0u : (unsigned int)(c & 0xff);
	}
public:
	FileDataSource(FILE *fp)
	{
		f = fp;
	};

	virtual ~FileDataSource() {};

	virtual unsigned int read1()
	{
		return getbyte();
	};

	virtual unsigned int read2()
	{
		unsigned int b0 = getbyte();
		unsigned int b1 = getbyte();
		return (b0 + (b1 << 8));
	};

	virtual unsigned int read2high()
	{
		unsigned int b1 = getbyte();
		unsigned int b0 = getbyte();
		return (b0 + (b1 << 8));
	};

	virtual unsigned int read4()
	{
		unsigned int b0 = getbyte();
		unsigned int b1 = getbyte();
		unsigned int b2 = getbyte();
		unsigned int b3 = getbyte();
		return (b0 + (b1<<8) + (b2<<16) + (b3<<24));
	};

	virtual unsigned int read4high()
	{
		unsigned int b3 = getbyte();
		unsigned int b2 = getbyte();
		unsigned int b1 = getbyte();
		unsigned int b0 = getbyte();
		return (b0 + (b1<<8) + (b2<<16) + (b3<<24));
	};

	void read(char *b, int len) {
		if (len <= 0) return;
		size_t bytesRead = fread(b, 1, (size_t) len, f);
		if (bytesRead < (size_t) len) {
			// zero-fill the remainder on a short read
			memset(b + bytesRead, 0, (size_t) len - bytesRead);
		}
	};

	virtual void write1(unsigned int val)
	{
		fputc((int)(val&0xff),f);
	};

	virtual void write2(unsigned int val)
	{
		fputc((int)(val&0xff),f);
		fputc((int)((val>>8)&0xff),f);
	};

	virtual void write2high(unsigned int val)
	{
		fputc((int)((val>>8)&0xff),f);
		fputc((int)(val&0xff),f);
	};

	virtual void write4(unsigned int val)
	{
		fputc((int)(val&0xff),f);
		fputc((int)((val>>8)&0xff),f);
		fputc((int)((val>>16)&0xff),f);
		fputc((int)((val>>24)&0xff),f);
	};

	virtual void write4high(unsigned int val)
	{
		fputc((int)((val>>24)&0xff),f);
		fputc((int)((val>>16)&0xff),f);
		fputc((int)((val>>8)&0xff),f);
		fputc((int)(val&0xff),f);
	};

	virtual void seek(unsigned int pos) {
		if (fseek(f, (long)pos, SEEK_SET) != 0) {
			// best-effort; caller has no recovery
		}
	};

	virtual void skip(int pos) {
		if (fseek(f, pos, SEEK_CUR) != 0) {
			// best-effort
		}
	};

	virtual unsigned int getSize()
	{
		long pos = ftell(f);
		if (pos < 0) return 0u;
		if (fseek(f, 0, SEEK_END) != 0) return 0u;
		long len = ftell(f);
		if (len < 0) len = 0;
		if (fseek(f, pos, SEEK_SET) != 0) {
			// best-effort
		}
		return (unsigned int)len;
	};

	virtual unsigned int getPos()
	{
		long p = ftell(f);
		return (p < 0) ? 0u : (unsigned int)p;
	};
};

class BufferDataSource: public DataSource
{
private:
	unsigned char *buf, *buf_ptr;
	unsigned int size;
public:
	BufferDataSource(char *data, unsigned int len)
	{
		buf = buf_ptr = (unsigned char*)data;
		size = len;
	};

	virtual ~BufferDataSource() {};

	virtual unsigned int read1()
	{
		unsigned char b0;
		b0 = (unsigned char)*buf_ptr++;
		return (b0);
	};

	virtual unsigned int read2()
	{
		unsigned char b0, b1;
		b0 = (unsigned char)*buf_ptr++;
		b1 = (unsigned char)*buf_ptr++;
		return (b0 + (b1 << 8));
	};

	virtual unsigned int read2high()
	{
		unsigned char b0, b1;
		b1 = (unsigned char)*buf_ptr++;
		b0 = (unsigned char)*buf_ptr++;
		return (b0 + (b1 << 8));
	};

	virtual unsigned int read4()
	{
		unsigned char b0, b1, b2, b3;
		b0 = (unsigned char)*buf_ptr++;
		b1 = (unsigned char)*buf_ptr++;
		b2 = (unsigned char)*buf_ptr++;
		b3 = (unsigned char)*buf_ptr++;
		return (b0 + (b1<<8) + (b2<<16) + (b3<<24));
	};

	virtual unsigned int read4high()
	{
		unsigned char b0, b1, b2, b3;
		b3 = (unsigned char)*buf_ptr++;
		b2 = (unsigned char)*buf_ptr++;
		b1 = (unsigned char)*buf_ptr++;
		b0 = (unsigned char)*buf_ptr++;
		return (b0 + (b1<<8) + (b2<<16) + (b3<<24));
	};

	void read(char *b, int len) {
		memcpy(b, buf_ptr, len);
		buf_ptr += len;
	};

	virtual void write1(unsigned int val)
	{
		*buf_ptr++ = val & 0xff;
	};

	virtual void write2(unsigned int val)
	{
		*buf_ptr++ = val & 0xff;
		*buf_ptr++ = (val>>8) & 0xff;
	};

	virtual void write2high(unsigned int val)
	{
		*buf_ptr++ = (val>>8) & 0xff;
		*buf_ptr++ = val & 0xff;
	};


	virtual void write4(unsigned int val)
	{
		*buf_ptr++ = val & 0xff;
		*buf_ptr++ = (val>>8) & 0xff;
		*buf_ptr++ = (val>>16)&0xff;
		*buf_ptr++ = (val>>24)&0xff;
	};

	virtual void write4high(unsigned int val)
	{
		*buf_ptr++ = (val>>24)&0xff;
		*buf_ptr++ = (val>>16)&0xff;
		*buf_ptr++ = (val>>8) & 0xff;
		*buf_ptr++ = val & 0xff;
	};

	virtual void seek(unsigned int pos) { buf_ptr = buf+pos; };

	virtual void skip(int pos) { buf_ptr += pos; };

	virtual unsigned int getSize() { return (size); };

	virtual unsigned int getPos() { return (buf_ptr-buf); };

	unsigned char *getPtr() { return (buf_ptr); };
};

#endif
