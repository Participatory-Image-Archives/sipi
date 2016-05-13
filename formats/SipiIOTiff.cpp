    /*
 * Copyright © 2016 Lukas Rosenthaler, Andrea Bianco, Benjamin Geer,
 * Ivan Subotic, Tobias Schweizer, André Kilchenmann, and André Fatton.
 * This file is part of Sipi.
 * Sipi is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * Sipi is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * Additional permission under GNU AGPL version 3 section 7:
 * If you modify this Program, or any covered work, by linking or combining
 * it with Kakadu (or a modified version of that library) or Adobe ICC Color
 * Profiles (or a modified version of that library) or both, containing parts
 * covered by the terms of the Kakadu Software Licence or Adobe Software Licence,
 * or both, the licensors of this Program grant you additional permission
 * to convey the resulting work.
 * See the GNU Affero General Public License for more details.
 * You should have received a copy of the GNU Affero General Public
 * License along with Sipi.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <assert.h>
#include <stdlib.h>
#include <string>
#include <iostream>
#include <fstream>
#include <cstdio>
#include <cmath>

#include <stdlib.h>

#include "Connection.h"
#include "SipiError.h"
#include "SipiIOTiff.h"

#include "tif_dir.h"  // libtiff internals; for _TIFFFieldArray


#include "Global.h"
#include "spdlog/spdlog.h"

static const char __file__[] = __FILE__;

#define TIFF_GET_FIELD(file,tag,var,default) {\
if (0 == TIFFGetField ((file), (tag), (var)))*(var) = (default); }


extern "C" {

    typedef struct _memtiff {
        unsigned char *data;
        tsize_t size;
        tsize_t incsiz;
        tsize_t flen;
        toff_t fptr;
    } MEMTIFF;

    static MEMTIFF *memTiffOpen(tsize_t incsiz = 10240, tsize_t initsiz = 10240)
    {
        MEMTIFF *memtif;
        if ((memtif = (MEMTIFF *) malloc(sizeof(MEMTIFF))) == NULL) {
            throw Sipi::SipiError(__file__, __LINE__, "malloc failed", errno);
        }
        memtif->incsiz = incsiz;
        if (initsiz == 0) initsiz = incsiz;
        if ((memtif->data = (unsigned char *) malloc(initsiz*sizeof(unsigned char))) == NULL) {
            free (memtif);
            throw Sipi::SipiError(__file__, __LINE__, "malloc failed", errno);
        }
        memtif->size = initsiz;
        memtif->flen = 0;
        memtif->fptr = 0;
        return memtif;
    }
    /*===========================================================================*/

    static tsize_t memTiffReadProc(thandle_t handle, tdata_t buf, tsize_t size)
    {
        MEMTIFF *memtif = (MEMTIFF *) handle;
        tsize_t n;
        if (((tsize_t) memtif->fptr + size) <= memtif->flen) {
            n = size;
        }
        else {
            n = memtif->flen - memtif->fptr;
        }
        memcpy(buf, memtif->data + memtif->fptr, n);
        memtif->fptr += n;

        return n;
    }
    /*===========================================================================*/

    static tsize_t memTiffWriteProc(thandle_t handle, tdata_t buf, tsize_t size)
    {
        MEMTIFF *memtif = (MEMTIFF *) handle;
        if (((tsize_t) memtif->fptr + size) > memtif->size) {
            if ((memtif->data = (unsigned char *) realloc(memtif->data, memtif->fptr + memtif->incsiz + size)) == NULL) {
                throw Sipi::SipiError(__file__, __LINE__, "realloc failed", errno);
            }
            memtif->size = memtif->fptr + memtif->incsiz + size;
        }
        memcpy (memtif->data + memtif->fptr, buf, size);
        memtif->fptr += size;
        if (memtif->fptr > memtif->flen) memtif->flen = memtif->fptr;

        return size;
    }
    /*===========================================================================*/

    static toff_t memTiffSeekProc(thandle_t handle, toff_t off, int whence)
    {
        MEMTIFF *memtif = (MEMTIFF *) handle;
        switch (whence) {
            case SEEK_SET: {
                if ((tsize_t) off > memtif->size) {
                    if ((memtif->data = (unsigned char *) realloc(memtif->data, memtif->size + memtif->incsiz + off)) == NULL) {
                        throw Sipi::SipiError(__file__, __LINE__, "realloc failed", errno);
                    }
                    memtif->size = memtif->size + memtif->incsiz + off;
                }
                memtif->fptr = off;
                break;
            }
            case SEEK_CUR: {
                if ((tsize_t)(memtif->fptr + off) > memtif->size) {
                    if ((memtif->data = (unsigned char *) realloc(memtif->data, memtif->fptr + memtif->incsiz + off)) == NULL) {
                        throw Sipi::SipiError(__file__, __LINE__, "realloc failed", errno);
                    }
                    memtif->size = memtif->fptr + memtif->incsiz + off;
                }
                memtif->fptr += off;
                break;
            }
            case SEEK_END: {
                if ((tsize_t) (memtif->size + off) > memtif->size) {
                    if ((memtif->data = (unsigned char *) realloc(memtif->data, memtif->size + memtif->incsiz + off)) == NULL) {
                        throw Sipi::SipiError(__file__, __LINE__, "realloc failed", errno);
                    }
                    memtif->size = memtif->size + memtif->incsiz + off;
                }
                memtif->fptr = memtif->size + off;
                break;
            }
        }
        if (memtif->fptr > memtif->flen) memtif->flen = memtif->fptr;
        return memtif->fptr;
    }
    /*===========================================================================*/

    static int memTiffCloseProc(thandle_t handle)
    {
        MEMTIFF *memtif = (MEMTIFF *) handle;
        memtif->fptr = 0;
        return 0;
    }
    /*===========================================================================*/


    static toff_t memTiffSizeProc(thandle_t handle)
    {
        MEMTIFF *memtif = (MEMTIFF *) handle;
        return memtif->flen;
    }
    /*===========================================================================*/


    static int memTiffMapProc(thandle_t handle, tdata_t* base, toff_t* psize)
    {
        MEMTIFF *memtif = (MEMTIFF *) handle;
        *base = memtif->data;
        *psize = memtif->flen;
        return (1);
    }
    /*===========================================================================*/

    static void memTiffUnmapProc(thandle_t handle, tdata_t base, toff_t size)
    {
        return;
    }
    /*===========================================================================*/

    static void memTiffFree(MEMTIFF *memtif)
    {
        if (memtif->data != NULL) free(memtif->data);
        memtif->data = NULL;
        if (memtif != NULL) free(memtif);
        memtif = NULL;
        return;
    }
    /*===========================================================================*/

}

using namespace std;


//
// the 2 typedefs below are used to extract the EXIF-tags from a TIFF file. This is done
// using the normal libtiff functions...
//
typedef enum {
	EXIF_DT_UINT8 = 1,
	EXIF_DT_STRING = 2,
	EXIF_DT_UINT16 = 3,
	EXIF_DT_UINT32 = 4,
	EXIF_DT_RATIONAL = 5,
	EXIF_DT_2ST = 7,

	EXIF_DT_RATIONAL_PTR =101,
	EXIF_DT_UINT8_PTR = 102,
	EXIF_DT_UINT16_PTR = 103,
	EXIF_DT_UINT32_PTR = 104,
	EXIF_DT_PTR = 105,
	EXIF_DT_UNDEFINED = 999

} ExifDataType_type;

typedef struct _exif_tag {
	int tag_id;
	ExifDataType_type datatype;
	int len;
	union {
		float f_val;
		uint8 c_val;
		uint16 s_val;
		uint32 i_val;
		char *str_val;
		float *f_ptr;
		uint8 *c_ptr;
		uint16 *s_ptr;
		uint32 *i_ptr;
		void *ptr;
		unsigned char _4cc[4];
		unsigned short _2st[2];
	};
} ExifTag_type;

static ExifTag_type exiftag_list[] = {
	{EXIFTAG_EXPOSURETIME, EXIF_DT_RATIONAL, 0L, {0L}},
	{EXIFTAG_FNUMBER, EXIF_DT_RATIONAL, 0L, {0L}},
	{EXIFTAG_EXPOSUREPROGRAM, EXIF_DT_UINT16, 0L, {0L}},
	{EXIFTAG_SPECTRALSENSITIVITY, EXIF_DT_STRING, 0L, {0L}},
	{EXIFTAG_ISOSPEEDRATINGS, EXIF_DT_UINT16_PTR, 0L, {0L}},
	{EXIFTAG_OECF, EXIF_DT_PTR, 0L, {0L}},
	{EXIFTAG_EXIFVERSION, EXIF_DT_UNDEFINED, 0L, {0L}},
	{EXIFTAG_DATETIMEORIGINAL, EXIF_DT_STRING, 0L, {0L}},
	{EXIFTAG_DATETIMEDIGITIZED,EXIF_DT_STRING, 0L, {0L}},
	{EXIFTAG_COMPONENTSCONFIGURATION, EXIF_DT_UNDEFINED, 0L, {1L}}, // !!!! would be 4cc
	{EXIFTAG_COMPRESSEDBITSPERPIXEL, EXIF_DT_RATIONAL, 0L, {0L}},
	{EXIFTAG_SHUTTERSPEEDVALUE, EXIF_DT_RATIONAL, 0L, {0L}},
	{EXIFTAG_APERTUREVALUE, EXIF_DT_RATIONAL, 0L, {0l}},
	{EXIFTAG_BRIGHTNESSVALUE, EXIF_DT_RATIONAL, 0L, {0l}},
	{EXIFTAG_EXPOSUREBIASVALUE, EXIF_DT_RATIONAL, 0L, {0l}},
	{EXIFTAG_MAXAPERTUREVALUE, EXIF_DT_RATIONAL, 0L, {0l}},
	{EXIFTAG_SUBJECTDISTANCE, EXIF_DT_RATIONAL, 0L, {0l}},
	{EXIFTAG_METERINGMODE, EXIF_DT_UINT16, 0L, {0l}},
	{EXIFTAG_LIGHTSOURCE, EXIF_DT_UINT16, 0L, {0l}},
	{EXIFTAG_FLASH, EXIF_DT_UINT16, 0L, {0l}},
	{EXIFTAG_FOCALLENGTH, EXIF_DT_RATIONAL, 0L, {0l}},
	{EXIFTAG_SUBJECTAREA, EXIF_DT_UINT16_PTR, 0L, {0L}}, //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! ARRAY OF SHORTS
	{EXIFTAG_MAKERNOTE, EXIF_DT_STRING, 0L, {0L}},
	{EXIFTAG_USERCOMMENT, EXIF_DT_PTR, 0L, {0L}},
	{EXIFTAG_SUBSECTIME, EXIF_DT_STRING, 0L, {0L}},
	{EXIFTAG_SUBSECTIMEORIGINAL, EXIF_DT_STRING, 0L, {0L}},
	{EXIFTAG_SUBSECTIMEDIGITIZED, EXIF_DT_STRING, 0L, {0L}},
	{EXIFTAG_FLASHPIXVERSION, EXIF_DT_UNDEFINED, 0L, {01L}}, // 2 SHORTS
	{EXIFTAG_COLORSPACE, EXIF_DT_UINT16, 0L, {0l}},
	{EXIFTAG_PIXELXDIMENSION, EXIF_DT_UINT32, 0L, {0l}}, // CAN ALSO BE UINT16 !!!!!!!!!!!!!!
	{EXIFTAG_PIXELYDIMENSION, EXIF_DT_UINT32, 0L, {0l}}, // CAN ALSO BE UINT16 !!!!!!!!!!!!!!
	{EXIFTAG_RELATEDSOUNDFILE, EXIF_DT_STRING, 0L, {0L}},
	{EXIFTAG_FLASHENERGY, EXIF_DT_RATIONAL, 0L, {0l}},
	{EXIFTAG_SPATIALFREQUENCYRESPONSE,  EXIF_DT_PTR, 0L, {0L}},
	{EXIFTAG_FOCALPLANEXRESOLUTION, EXIF_DT_RATIONAL, 0L, {0l}},
	{EXIFTAG_FOCALPLANEYRESOLUTION, EXIF_DT_RATIONAL, 0L, {0l}},
	{EXIFTAG_FOCALPLANERESOLUTIONUNIT,  EXIF_DT_UINT16, 0L, {0l}},
	{EXIFTAG_SUBJECTLOCATION, EXIF_DT_UINT32, 0L, {0l}}, // 2 SHORTS !!!!!!!!!!!!!!!!!!!!!!!!!!!
	{EXIFTAG_EXPOSUREINDEX, EXIF_DT_RATIONAL, 0L, {0l}},
	{EXIFTAG_SENSINGMETHOD, EXIF_DT_UINT16, 0L, {0l}},
	{EXIFTAG_FILESOURCE, EXIF_DT_UINT8, 0L, {0L}},
	{EXIFTAG_SCENETYPE, EXIF_DT_UINT8, 0L, {0L}},
	{EXIFTAG_CFAPATTERN, EXIF_DT_PTR, 0L, {0L}},
	{EXIFTAG_CUSTOMRENDERED, EXIF_DT_UINT16, 0L, {0l}},
	{EXIFTAG_EXPOSUREMODE, EXIF_DT_UINT16, 0L, {0l}},
	{EXIFTAG_WHITEBALANCE, EXIF_DT_UINT16, 0L, {0l}},
	{EXIFTAG_DIGITALZOOMRATIO, EXIF_DT_RATIONAL, 0L, {0l}},
	{EXIFTAG_FOCALLENGTHIN35MMFILM, EXIF_DT_UINT16, 0L, {0l}},
	{EXIFTAG_SCENECAPTURETYPE, EXIF_DT_UINT16, 0L, {0l}},
	{EXIFTAG_GAINCONTROL, EXIF_DT_UINT16, 0L, {0l}},
	{EXIFTAG_CONTRAST, EXIF_DT_UINT16, 0L, {0l}},
	{EXIFTAG_SATURATION, EXIF_DT_UINT16, 0L, {0l}},
	{EXIFTAG_SHARPNESS, EXIF_DT_UINT16, 0L, {0l}},
	{EXIFTAG_DEVICESETTINGDESCRIPTION, EXIF_DT_PTR, 0L, {0L}},
	{EXIFTAG_SUBJECTDISTANCERANGE, EXIF_DT_UINT16, 0L, {0L}},
	{EXIFTAG_IMAGEUNIQUEID, EXIF_DT_PTR, 33L, {0L}},
};

static int exiftag_list_len = sizeof(exiftag_list) / sizeof(ExifTag_type);


namespace Sipi {

    unsigned char *read_watermark(std::string wmfile, int &nx, int &ny, int &nc) {
        TIFF *tif;
        int sll;
        unsigned short spp, bps, pmi, pc;
        byte *wmbuf;

        nx = 0;
        ny = 0;
        if (NULL == (tif = TIFFOpen(wmfile.c_str(), "r"))) {
            return NULL;
        }

        // add EXIF tags to the set of tags that libtiff knows about
        // necessary if we want to set EXIFTAG_DATETIMEORIGINAL, for example
        //const TIFFFieldArray *exif_fields = _TIFFGetExifFields();
        //_TIFFMergeFields(tif, exif_fields->fields, exif_fields->count);


        if (TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &nx) == 0) {
            TIFFClose(tif);
            throw SipiError(__file__, __LINE__, "ERROR in read_watermark: TIFFGetField of TIFFTAG_IMAGEWIDTH failed: " + wmfile);
        }

        if (TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &ny) == 0) {
            TIFFClose(tif);
            throw SipiError(__file__, __LINE__, "ERROR in read_watermark: TIFFGetField of TIFFTAG_IMAGELENGTH failed: " + wmfile);
        }

        TIFF_GET_FIELD (tif, TIFFTAG_SAMPLESPERPIXEL, &spp, 1);
        if (spp != 1) {
            TIFFClose(tif);
            throw SipiError(__file__, __LINE__, "ERROR in read_watermark: ssp ≠ 1: " + wmfile);
        }

        TIFF_GET_FIELD (tif, TIFFTAG_BITSPERSAMPLE, &bps, 1);
        if (bps != 8) {
            TIFFClose(tif);
            throw SipiError(__file__, __LINE__, "ERROR in read_watermark: bps ≠ 8: " + wmfile);
        }

        TIFF_GET_FIELD (tif, TIFFTAG_PHOTOMETRIC, &pmi, PHOTOMETRIC_MINISBLACK);

        TIFF_GET_FIELD (tif, TIFFTAG_PLANARCONFIG, &pc, PLANARCONFIG_CONTIG);
        if (pc != PLANARCONFIG_CONTIG) {
            TIFFClose(tif);
            throw SipiError(__file__, __LINE__, "ERROR in read_watermark: Tag TIFFTAG_PLANARCONFIG is not PLANARCONFIG_CONTIG: " + wmfile);
        }
        sll = nx*spp*bps/8;
        try {
            wmbuf = new byte[ny*sll];
        }
        catch (std::bad_alloc& ba){
            throw SipiError(__file__, __LINE__, "ERROR in read_watermark: Could not allocate memory: "); // + ba.what());
        }

        int cnt = 0;
        for (int i = 0; i < ny; i++) {
            if (TIFFReadScanline (tif, wmbuf + i*sll, i) == -1) {
                delete [] wmbuf;
                throw SipiError(__file__, __LINE__, "ERROR in read_watermark: TIFFReadScanline failed on scanline" + to_string(i) + " File: " + wmfile);
            }
            for (int ii = 0; ii < sll; ii++) {
                if (wmbuf[i*sll + ii] > 0) {
                    cnt++;
                }
            }
        }

        TIFFClose(tif);
        nc = spp;

        return wmbuf;
    }
    //============================================================================


    static void tiffError(const char* module, const char* fmt, va_list argptr)
    {
        auto logger = spdlog::get(shttps::loggername);
        if (logger != NULL) {
            char errmsg[512];
            vsnprintf(errmsg, 511, fmt, argptr);
            logger->error("ERROR IN TIFF! Module: ") << module << " " << errmsg;
        }
        else {
            cerr << "ERROR IN TIFF! Module: " << module << endl;
            vfprintf (stderr, fmt, argptr);
            cerr << "=============================" << endl;
        }
        return;
    }
    //============================================================================


    static void tiffWarning(const char* module, const char* fmt, va_list argptr)
    {
        auto logger = spdlog::get(shttps::loggername);
        if (logger != NULL) {
            char errmsg[512];
            vsnprintf(errmsg, 511, fmt, argptr);
            logger->warn("ERROR IN TIFF! Module: ") << module << " " << errmsg;
        }
        else {
            cerr << "WARNING IN TIFF! Module: " << module << endl;
            vfprintf (stderr, fmt, argptr);
            cerr << "=============================" << endl;
        }
        return;
    }
    //============================================================================


    bool SipiIOTiff::read(SipiImage *img, string filepath, SipiRegion *region, SipiSize *size)
    {
    	TIFF *tif;

        auto logger = spdlog::get(shttps::loggername);

        TIFFSetWarningHandler(NULL);
        TIFFSetErrorHandler(NULL);
        if (NULL != (tif = TIFFOpen (filepath.c_str(), "r"))) {
            TIFFSetErrorHandler(tiffError);
            TIFFSetWarningHandler(tiffWarning);

            //
            // OK, it's a TIFF file
            //
            uint16 safo, ori, planar, stmp;

            (void) TIFFSetWarningHandler(NULL);

            if (TIFFGetField (tif, TIFFTAG_IMAGEWIDTH, &(img->nx)) == 0) {
                cerr << "TIFF image file \"" << filepath << "\" Error getting TIFFTAG_IMAGEWIDTH !" << endl;
                TIFFClose(tif);
                string msg = "TIFFGetField of TIFFTAG_IMAGEWIDTH failed: " + filepath;
                throw SipiError(__file__, __LINE__, msg);
            }
            if (TIFFGetField (tif, TIFFTAG_IMAGELENGTH, &(img->ny)) == 0) {
                TIFFClose(tif);
                string msg = "TIFFGetField of TIFFTAG_IMAGELENGTH failed: " + filepath;
                throw SipiError(__file__, __LINE__, msg);
            }
            unsigned int sll = (unsigned int) TIFFScanlineSize (tif);
            TIFF_GET_FIELD (tif, TIFFTAG_SAMPLESPERPIXEL, &stmp, 1);
            img->nc = (int) stmp;

            TIFF_GET_FIELD (tif, TIFFTAG_BITSPERSAMPLE, &stmp, 1);
            img->bps = stmp;
            TIFF_GET_FIELD (tif, TIFFTAG_ORIENTATION, &ori, ORIENTATION_TOPLEFT);
            if (1 != TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &stmp)) {
                img->photo = MINISBLACK;
            }
            else {
                img->photo = (PhotometricInterpretation) stmp;
            }
            TIFF_GET_FIELD (tif, TIFFTAG_PLANARCONFIG, &planar, PLANARCONFIG_CONTIG);
            TIFF_GET_FIELD (tif, TIFFTAG_SAMPLEFORMAT, &safo, SAMPLEFORMAT_UINT);

            unsigned short *es;
            int eslen;
            if (TIFFGetField(tif, TIFFTAG_EXTRASAMPLES, &eslen, &es) == 1) {
                img->ne = eslen;
                img->es = new ExtraSamples[eslen];
                for (int i = 0; i < eslen; i++) img->es[i] = (ExtraSamples) es[i];
            }

            //
            // reading TIFF Meatdata and adding the fields to the exif header.
            // We store the TIFF metadata in the private exifData member variable using addKeyVal.
            //
            char *str;
            if (1 == TIFFGetField(tif, TIFFTAG_IMAGEDESCRIPTION, &str)) {
                if (img->exif == NULL) img->exif = new SipiExif();
                img->exif->addKeyVal(string("Exif.Image.ImageDescription"), string(str));
            }
            if (1 == TIFFGetField(tif, TIFFTAG_MAKE, &str)) {
                if (img->exif == NULL) img->exif = new SipiExif();
                img->exif->addKeyVal(string("Exif.Image.Make"), string(str));
            }
            if (1 == TIFFGetField(tif, TIFFTAG_MODEL, &str)) {
                if (img->exif == NULL) img->exif = new SipiExif();
                img->exif->addKeyVal(string("Exif.Image.Model"), string(str));
            }
            if (1 == TIFFGetField(tif, TIFFTAG_SOFTWARE, &str)) {
                if (img->exif == NULL) img->exif = new SipiExif();
                img->exif->addKeyVal(string("Exif.Image.Software"), string(str));
            }
            if (1 == TIFFGetField(tif, TIFFTAG_DATETIME, &str)) {
                if (img->exif == NULL) img->exif = new SipiExif();
                img->exif->addKeyVal(string("Exif.Image.DateTime"), string(str));
            }
            if (1 == TIFFGetField(tif, TIFFTAG_ARTIST, &str)) {
                if (img->exif == NULL) img->exif = new SipiExif();
                img->exif->addKeyVal(string("Exif.Image.Artist"), string(str));
            }
            if (1 == TIFFGetField(tif, TIFFTAG_HOSTCOMPUTER, &str)) {
                if (img->exif == NULL) img->exif = new SipiExif();
                img->exif->addKeyVal(string("Exif.Image.HostComputer"), string(str));
            }
            if (1 == TIFFGetField(tif, TIFFTAG_COPYRIGHT, &str)) {
                if (img->exif == NULL) img->exif = new SipiExif();
                img->exif->addKeyVal(string("Exif.Image.Copyright"), string(str));
            }
            if (1 == TIFFGetField(tif, TIFFTAG_DOCUMENTNAME, &str)) {
                if (img->exif == NULL) img->exif = new SipiExif();
                img->exif->addKeyVal(string("Exif.Image.DocumentName"), string(str));
            }

            // ???????? What shall we do with this meta data which is not standard in exif??????
            // We could add it as Xmp?
            //
/*
            if (1 == TIFFGetField(tif, TIFFTAG_PAGENAME, &str)) {
                if (img->exif == NULL) img->exif = new SipiExif();
                img->exif->addKeyVal(string("Exif.Image.PageName"), string(str));
            }
            if (1 == TIFFGetField(tif, TIFFTAG_PAGENUMBER, &str)) {
                if (img->exif == NULL) img->exif = new SipiExif();
                img->exif->addKeyVal(string("Exif.Image.PageNumber"), string(str));
            }
*/
            float f;
            if (1 == TIFFGetField(tif, TIFFTAG_XRESOLUTION, &f)) {
                if (img->exif == NULL) img->exif = new SipiExif();
                img->exif->addKeyVal(string("Exif.Image.XResolution"), f);
            }
            if (1 == TIFFGetField(tif, TIFFTAG_YRESOLUTION, &f)) {
                if (img->exif == NULL) img->exif = new SipiExif();
                img->exif->addKeyVal(string("Exif.Image.YResolution"), f);
            }

            short s;
        	if (1 == TIFFGetField(tif, TIFFTAG_RESOLUTIONUNIT, &s)) {
                if (img->exif == NULL) img->exif = new SipiExif();
                img->exif->addKeyVal(string("Exif.Image.ResolutionUnit"), s);
        	}


            //
            // read iptc header
            //
            unsigned int iptc_length = 0;
            unsigned char *iptc_content = NULL;

            if (TIFFGetField(tif, TIFFTAG_RICHTIFFIPTC, &iptc_length, &iptc_content) != 0) {
                try {
                    img->iptc = new SipiIptc(iptc_content, iptc_length);
                }
                catch (SipiError &err) {
                    logger != NULL ? logger << err : cerr << err;
                }
            }


            //
            // read exif here....
            //
            toff_t exif_ifd_offs;
            if (1 == TIFFGetField(tif, TIFFTAG_EXIFIFD, &exif_ifd_offs)) {
                if (img->exif == NULL) img->exif = new SipiExif();
                readExif(img, tif, exif_ifd_offs);
            }

            //
            // read xmp header
            //
            int xmp_length;
            char *xmp_content = NULL;
            if (1 == TIFFGetField(tif, TIFFTAG_XMLPACKET, &xmp_length, &xmp_content)) {
                try {
                    img->xmp = new SipiXmp(xmp_content, xmp_length);
                }
                catch (SipiError &err) {
                    logger != NULL ? logger << err : cerr << err;
                }
            }


            //
            // Read ICC-profile
            //
            unsigned int icc_len;
            unsigned char *icc_buf;
            float *whitepoint = NULL;
            if (1 == TIFFGetField(tif, TIFFTAG_ICCPROFILE, &icc_len, &icc_buf)) {
                try {
                    img->icc = new SipiIcc(icc_buf, icc_len);
                }
                catch (SipiError &err) {
                    logger != NULL ? logger << err : cerr << err;
                }
            }
            else if (1 == TIFFGetField(tif, TIFFTAG_WHITEPOINT, &whitepoint)) {
                //
                // Wow, we have TIFF colormetry..... Who is still using this???
                //
                float *primaries_ti = NULL;
                float *primaries;
                bool primaries_del = false;
                if (1 == TIFFGetField(tif, TIFFTAG_PRIMARYCHROMATICITIES, &primaries_ti)) {
                    primaries[0] = primaries_ti[0];
                    primaries[1] = primaries_ti[1];
                    primaries[2] = primaries_ti[2];
                    primaries[3] = primaries_ti[3];
                    primaries[4] = primaries_ti[4];
                    primaries[5] = primaries_ti[5];
                }
                else {
                    //
                    // not defined, let's take the sRGB primaries
                    //
                    primaries = new float[6];
                    primaries[0] = 0.6400;
                    primaries[1] = 0.3300;
                    primaries[2] = 0.3000;
                    primaries[3] = 0.6000;
                    primaries[4] = 0.1500;
                    primaries[5] = 0.0600;
                    primaries_del = true;
                }
                unsigned short *tfunc, *tfunc_ti = new unsigned short[3*(1 << img->bps)];
                unsigned int tfunc_len, tfunc_len_ti;
                if (1 == TIFFGetField(tif, TIFFTAG_PRIMARYCHROMATICITIES, &tfunc_len_ti, &tfunc_ti)) {
                    if ((tfunc_len_ti / (1 << img->bps)) == 1) {
                        memcpy(tfunc, tfunc_ti, tfunc_len_ti);
                        memcpy(tfunc + tfunc_len_ti, tfunc_ti, tfunc_len_ti);
                        memcpy(tfunc + 2*tfunc_len_ti, tfunc_ti, tfunc_len_ti);
                        tfunc_len = tfunc_len_ti;
                    }
                    else {
                        memcpy(tfunc, tfunc_ti, tfunc_len_ti);
                        tfunc_len = tfunc_len_ti / 3;
                    }
                }
                else {
                    tfunc = NULL;
                    tfunc_len = 0;
                }
                img->icc = new SipiIcc(whitepoint, primaries, tfunc, tfunc_len);
                if (tfunc != NULL) delete [] tfunc;
                if (primaries_del) delete [] primaries;
            }


            if ((region == NULL) || (region->getType() == SipiRegion::FULL)) {
                if (planar == PLANARCONFIG_CONTIG) {
                    uint32 i;
                    uint8 *dataptr = new uint8[img->ny*sll];
                    for (i = 0; i < img->ny; i++) {
                        if (TIFFReadScanline(tif, dataptr + i*sll, i, 0) == -1) {
                            delete [] dataptr;
                            TIFFClose(tif);
                            string msg = "TIFFReadScanline failed on scanline" + to_string(i) + " File: " + filepath;
                            throw SipiError(__file__, __LINE__, msg);
                        }
                    }
                    img->pixels = dataptr;
                }
        		else if (planar == PLANARCONFIG_SEPARATE) { // RRRRR…RRR GGGGG…GGGG BBBBB…BBB
                    uint8 *dataptr = new uint8[img->nc*img->ny*sll];
                    for (uint32 j = 0; j < img->nc; j++) {
                        for (uint32 i = 0; i < img->ny; i++) {
                            if (TIFFReadScanline(tif, dataptr + j*img->ny*sll + i*sll, i, j) == -1) {
                                delete [] dataptr;
                                TIFFClose(tif);
                                string msg = "TIFFReadScanline failed on scanline" + to_string(i) + " File: " + filepath;
                                throw SipiError(__file__, __LINE__, msg);
                            }
                        }
                    }
                    img->pixels = dataptr;
                    //
                    // rearrange the data to RGBRGBRGB…RGB
                    //
                    separateToContig(img, sll); // convert to RGBRGBRGB...
                }
            }
            else {
                int roi_x, roi_y, roi_w, roi_h;
                region->crop_coords(img->nx, img->ny, roi_x, roi_y, roi_w, roi_h);

                int ps; // pixel size in bytes
                switch (img->bps) {
                    case 1: {
                        string msg = "Images with 1 bit/sample not supported! File: " + filepath;
                        throw SipiError(__file__, __LINE__, msg);
                    }
                    case 8: {
                        ps = 1;
                        break;
                    }
                    case 16: {
                        ps = 2;
                        break;
                    }
                }
                uint8 *dataptr = new uint8[sll];
                uint8 *inbuf = new uint8[ps*roi_w*roi_h*img->nc];
                if (planar == PLANARCONFIG_CONTIG) { // RGBRGBRGBRGBRGBRGBRGBRGB
                    for (uint32 i = 0; i < roi_h; i++) {
                        if (TIFFReadScanline(tif, dataptr, roi_y + i, 0) == -1) {
                            delete [] dataptr;
                            delete [] inbuf;
                            TIFFClose(tif);
                            string msg = "TIFFReadScanline failed on scanline" + to_string(i) + " File: " + filepath;
                            throw SipiError(__file__, __LINE__, msg);
                        }
                        memcpy (inbuf + ps*i*roi_w*img->nc, dataptr + ps*roi_x*img->nc, ps*roi_w*img->nc);
                    }
                    img->nx = roi_w;
                    img->ny = roi_h;
                    img->pixels = inbuf;
                }
                else if (planar == PLANARCONFIG_SEPARATE) { // RRRRR…RRR GGGGG…GGGG BBBBB…BBB
                    for (uint32 j = 0; j < img->nc; j++) {
                        for (uint32 i = 0; i < roi_h; i++) {
                            if (TIFFReadScanline(tif, dataptr, roi_y + i, j) == -1) {
                                delete [] dataptr;
                                delete [] inbuf;
                                TIFFClose(tif);
                                string msg = "TIFFReadScanline failed on scanline" + to_string(i) + " File: " + filepath;
                                throw SipiError(__file__, __LINE__, msg);
                            }
                            memcpy (inbuf + ps*roi_w*(j*roi_h + i), dataptr + ps*roi_x, ps*roi_w);
                        }
                    }
                    img->nx = roi_w;
                    img->ny = roi_h;
                    img->pixels = inbuf;
                    //
                    // rearrange the data to RGBRGBRGB…RGB
                    //
                    separateToContig(img, roi_w*ps); // convert to RGBRGBRGB...
                }
                delete [] dataptr;
            }

            TIFFClose(tif);

            if (img->icc == NULL) {
                switch(img->photo) {
                    case MINISBLACK: {
                        if (img->bps == 1) {
                            cvrt1BitTo8Bit(img, sll, 0, 255);
                        }
                        img->icc = new SipiIcc(icc_GRAY_D50);
                        break;
                    }
                    case MINISWHITE: {
                        if (img->bps == 1) {
                            cvrt1BitTo8Bit(img, sll, 255, 0);
                        }
                        img->icc = new SipiIcc(icc_GRAY_D50);
                        break;
                    }
                    case SEPARATED: {
                        img->icc = new SipiIcc(icc_CYMK_standard);
                        break;
                    }
                    case YCBCR: // fall through!
                    case RGB: {
                        img->icc = new SipiIcc(icc_sRGB);
                        break;
                    }
                    default: {
                        throw SipiError(__file__, __LINE__, "Unsupported photometric interpretation (" + to_string(img->photo) + ")!");
                    }
                }
            }
            /*
            if ((img->nc == 3) && (img->photo == PHOTOMETRIC_YCBCR)) {
                SipiIcc *target_profile = new SipiIcc(img->icc);
                switch (img->bps) {
                    case 8: {
                        img->convertToIcc(target_profile, TYPE_YCbCr_8);
                        break;
                    }
                    case 16: {
                        img->convertToIcc(target_profile, TYPE_YCbCr_16);
                        break;
                    }
                    default: {
                        throw SipiError(__file__, __LINE__, "Unsupported bits/sample (" + to_string(bps) + ")!");
                    }
                }
            }
            else if ((img->nc == 4) && (img->photo == PHOTOMETRIC_SEPARATED)) { // CMYK image
                SipiIcc *target_profile = new SipiIcc(icc_sRGB);
                switch (img->bps) {
                    case 8: {
                        img->convertToIcc(target_profile, TYPE_CMYK_8);
                        break;
                    }
                    case 16: {
                        img->convertToIcc(target_profile, TYPE_CMYK_16);
                        break;
                    }
                    default: {
                        throw SipiError(__file__, __LINE__, "Unsupported bits/sample (" + to_string(bps) + ")!");
                    }
                }
            }
            */

            //
            // resize/Scale the image if necessary
            //
            if (size != NULL) {
                int nnx, nny, reduce;
                bool redonly;
                SipiSize::SizeType rtype = size->get_size(img->nx, img->ny, nnx, nny, reduce, redonly);
                if (rtype != SipiSize::FULL) {
                    img->scale(nnx, nny);
                }
            }

            return true;
        }
        return false;
    }
    //============================================================================


    bool SipiIOTiff::getDim(std::string filepath, int &width, int &height) {
    	TIFF *tif;

        TIFFSetWarningHandler(NULL);
        TIFFSetErrorHandler(NULL);
        if (NULL != (tif = TIFFOpen (filepath.c_str(), "r"))) {
            TIFFSetErrorHandler(tiffError);
            TIFFSetWarningHandler(tiffWarning);

            //
            // OK, it's a TIFF file
            //
            (void) TIFFSetWarningHandler(NULL);

            if (TIFFGetField (tif, TIFFTAG_IMAGEWIDTH, &width) == 0) {
                cerr << "TIFF image file \"" << filepath << "\" Error getting TIFFTAG_IMAGEWIDTH !" << endl;
                TIFFClose(tif);
                string msg = "TIFFGetField of TIFFTAG_IMAGEWIDTH failed: " + filepath;
                throw SipiError(__file__, __LINE__, msg);
            }
            if (TIFFGetField (tif, TIFFTAG_IMAGELENGTH, &height) == 0) {
                TIFFClose(tif);
                string msg = "TIFFGetField of TIFFTAG_IMAGELENGTH failed: " + filepath;
                throw SipiError(__file__, __LINE__, msg);
            }
            TIFFClose(tif);
            return true;
        }
        return false;
    }
    //============================================================================


    void SipiIOTiff::write(SipiImage *img, string filepath, int quality)
    {
        TIFF *tif;
        MEMTIFF *memtif = NULL;

        auto logger = spdlog::get(shttps::loggername);

        int rows_per_strip = 65536 * img->bps / (img->nc * img->nx * 8);
        if (rows_per_strip == 0) rows_per_strip = 1;

        TIFFSetWarningHandler(tiffWarning);
        TIFFSetErrorHandler(tiffError);

        if ((filepath == "-") || (filepath == "HTTP")) {
            memtif = memTiffOpen();
            tif = TIFFClientOpen("MEMTIFF", "wb", (thandle_t) memtif,
                memTiffReadProc,
                memTiffWriteProc,
                memTiffSeekProc,
                memTiffCloseProc,
                memTiffSizeProc,
                memTiffMapProc,
                memTiffUnmapProc
            );
        }
        else {
            if ((tif = TIFFOpen (filepath.c_str(), "wb")) == NULL) {
                if (memtif != NULL) memTiffFree(memtif);
                string msg = "TIFFopen of \"" + filepath + "\" failed!";
                throw SipiError(__file__, __LINE__, msg);
            }
        }
        TIFFSetField (tif, TIFFTAG_IMAGEWIDTH,      img->nx);
        TIFFSetField (tif, TIFFTAG_IMAGELENGTH,     img->ny);
        TIFFSetField (tif, TIFFTAG_ROWSPERSTRIP,    rows_per_strip);
        TIFFSetField (tif, TIFFTAG_ORIENTATION,     ORIENTATION_TOPLEFT);
        TIFFSetField (tif, TIFFTAG_BITSPERSAMPLE,   (uint16) img->bps);
        TIFFSetField (tif, TIFFTAG_SAMPLESPERPIXEL, (uint16) img->nc);
        TIFFSetField (tif, TIFFTAG_PHOTOMETRIC, img->photo);
        TIFFSetField (tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
        if (img->ne > 0) {
            TIFFSetField (tif, TIFFTAG_EXTRASAMPLES, img->ne, img->es);
        }

        //
        // let's get the TIFF metadata if there is some. We stored the TIFF metadata in the exifData meber variable!
        //
        if ((img->exif != NULL) & (!(img->skip_metadata & SKIP_EXIF))) {
            string value;
            if (img->exif->getValByKey("Exif.Image.ImageDescription", value)) {
                TIFFSetField(tif, TIFFTAG_IMAGEDESCRIPTION, value.c_str());
            }
            if (img->exif->getValByKey("Exif.Image.Make", value)) {
                TIFFSetField(tif, TIFFTAG_MAKE, value.c_str());
            }
            if (img->exif->getValByKey("Exif.Image.Model", value)) {
                TIFFSetField(tif, TIFFTAG_MODEL, value.c_str());
            }
            if (img->exif->getValByKey("Exif.Image.Software", value)) {
                TIFFSetField(tif, TIFFTAG_SOFTWARE, value.c_str());
            }
            if (img->exif->getValByKey("Exif.Image.DateTime", value)) {
                TIFFSetField(tif, TIFFTAG_DATETIME, value.c_str());
            }
            if (img->exif->getValByKey("Exif.Image.Artist", value)) {
                TIFFSetField(tif, TIFFTAG_ARTIST, value.c_str());
            }
            if (img->exif->getValByKey("Exif.Image.HostComputer", value)) {
                TIFFSetField(tif, TIFFTAG_HOSTCOMPUTER, value.c_str());
            }
            if (img->exif->getValByKey("Exif.Image.Copyright", value)) {
                TIFFSetField(tif, TIFFTAG_COPYRIGHT, value.c_str());
            }
            if (img->exif->getValByKey("Exif.Image.DocumentName", value)) {
                TIFFSetField(tif, TIFFTAG_DOCUMENTNAME, value.c_str());
            }
            float f;
            if (img->exif->getValByKey("Exif.Image.XResolution", f)) {
                TIFFSetField(tif, TIFFTAG_XRESOLUTION, f);
            }
            if (img->exif->getValByKey("Exif.Image.YResolution", f)) {
                TIFFSetField(tif, TIFFTAG_XRESOLUTION, f);
            }
            short s;
            if (img->exif->getValByKey("Exif.Image.ResolutionUnit", s)) {
                TIFFSetField(tif, TIFFTAG_RESOLUTIONUNIT, s);
            }
        }

        if ((img->icc != NULL) & (!(img->skip_metadata & SKIP_ICC))) {
            unsigned int len;
            const unsigned char *buf;
            try {
                buf = img->icc->iccBytes(len);
                if (len > 0) {
                    TIFFSetField(tif, TIFFTAG_ICCPROFILE, len, buf);
                }
            }
            catch (SipiError &err) {
                logger != NULL ? logger << err : cerr << err;
            }
        }

        //
        // write IPTC data, if available
        //
        if ((img->iptc != NULL) & (!(img->skip_metadata & SKIP_IPTC))) {
            unsigned int len;
            unsigned char *buf;
            try {
                buf = img->iptc->iptcBytes(len);
                if (len > 0) {
                    TIFFSetField(tif, TIFFTAG_RICHTIFFIPTC, len, buf);
                }
                delete [] buf;
            }
            catch (SipiError &err) {
                logger != NULL ? logger << err : cerr << err;
            }
        }


        //
        // write XMP data
        //
        if ((img->xmp != NULL) & (!(img->skip_metadata & SKIP_XMP))) {
            unsigned int len;
            const char *buf;
            try {
                buf = img->xmp->xmpBytes(len);
                if (len > 0) {
                    TIFFSetField(tif, TIFFTAG_XMLPACKET, len, buf);
                }
            }
            catch (SipiError &err) {
                logger != NULL ? logger << err : cerr << err;
            }
        }

        TIFFCheckpointDirectory(tif);

        for (int i = 0; i < img->ny; i++) {
            TIFFWriteScanline (tif, img->pixels + i * img->nc * img->nx * (img->bps / 8), i, 0);
        }

        TIFFWriteDirectory(tif);

        //
        // write exif data
        //
        if (img->exif != NULL) {
            writeExif(img, tif);
        }


        TIFFClose(tif);

        if (memtif != NULL) {
            if (filepath == "-") {
                size_t n = 0;
                while (n < memtif->flen) {
                    n += fwrite (&(memtif->data[n]), 1, memtif->flen - n > 10240 ? 10240 : memtif->flen - n, stdout);
                }
                fflush(stdout);
            }
            else if (filepath == "HTTP") {
                shttps::Connection *conobj = img->connection();
                try {
                    conobj->sendAndFlush(memtif->data, memtif->flen);
                }
                catch (int i) {
                    memTiffFree(memtif);
                    throw SipiError(__file__, __LINE__, "Sending data failed! Broken pipe?: " + filepath + " !");
                }
            }
            else {
                memTiffFree(memtif);
                throw SipiError(__file__, __LINE__, "Unknown output method: " + filepath + " !");
            }
            memTiffFree(memtif);
        }
    }
    //============================================================================

    void SipiIOTiff::readExif(SipiImage *img, TIFF *tif, toff_t exif_offset) {
        uint16 curdir = TIFFCurrentDirectory(tif);

        if (TIFFReadEXIFDirectory(tif, exif_offset)) {
            for (int i = 0; i < exiftag_list_len; i++) {
                switch (exiftag_list[i].datatype) {
                    case EXIF_DT_RATIONAL: {
                        float f;
                        if (TIFFGetField (tif, exiftag_list[i].tag_id, &f)) {
                            Exiv2::Rational r = SipiExif::toRational(f);
                            img->exif->addKeyVal(exiftag_list[i].tag_id, "Photo", r);
                        }
                        break;
                    }
                    case EXIF_DT_UINT8: {
                        unsigned char uc;
                        if (TIFFGetField (tif, exiftag_list[i].tag_id, &uc)) {
                            img->exif->addKeyVal(exiftag_list[i].tag_id, "Photo", uc);
                        }
                        break;
                    }
                    case EXIF_DT_UINT16: {
                        unsigned short us;
                        if (TIFFGetField (tif, exiftag_list[i].tag_id, &us)) {
                            img->exif->addKeyVal(exiftag_list[i].tag_id, "Photo", us);
                        }
                        break;
                    }
                    case EXIF_DT_UINT32: {
                        unsigned int ui;
                        if (TIFFGetField (tif, exiftag_list[i].tag_id, &ui)) {
                            img->exif->addKeyVal(exiftag_list[i].tag_id, "Photo", ui);
                        }
                        break;
                    }
                    case EXIF_DT_STRING: {
                        char *tmpstr = NULL;
                        if (TIFFGetField (tif, exiftag_list[i].tag_id, &tmpstr)) {
                            img->exif->addKeyVal(exiftag_list[i].tag_id, "Photo", tmpstr);
                        }
                        break;
                    }
                    case EXIF_DT_RATIONAL_PTR: {
                        float *tmpbuf;
                        unsigned int len;
                        if (TIFFGetField (tif, exiftag_list[i].tag_id, &len, &tmpbuf)) {
                            Exiv2::Rational *r = new Exiv2::Rational[len];
                            for (int i; i < len; i++) {
                                r[i] = SipiExif::toRational(tmpbuf[i]);
                            }
                            img->exif->addKeyVal(exiftag_list[i].tag_id, "Photo", r, len);
                            delete [] r;
                        }
                        break;
                    }
                    case EXIF_DT_UINT8_PTR: {
                        uint8 *tmpbuf;
                        unsigned int len;
                        if (TIFFGetField (tif, exiftag_list[i].tag_id, &len, &tmpbuf)) {
                            img->exif->addKeyVal(exiftag_list[i].tag_id, "Photo", tmpbuf, len);
                        }
                        break;
                    }
                    case EXIF_DT_UINT16_PTR: {
                        uint16 *tmpbuf;
                        unsigned int len;
                        if (TIFFGetField (tif, exiftag_list[i].tag_id, &len, &tmpbuf)) {
                            img->exif->addKeyVal(exiftag_list[i].tag_id, "Photo", tmpbuf, len);
                        }
                        break;
                    }
                    case EXIF_DT_UINT32_PTR: {
                        uint32 *tmpbuf;
                        unsigned int len;
                        if (TIFFGetField (tif, exiftag_list[i].tag_id, &len, &tmpbuf)) {
                            img->exif->addKeyVal(exiftag_list[i].tag_id, "Photo", tmpbuf, len);
                        }
                        break;
                    }
                    case EXIF_DT_PTR: {
                        unsigned char *tmpbuf;
                        unsigned int len;
                        if (exiftag_list[i].len == 0) {
                            if (TIFFGetField (tif, exiftag_list[i].tag_id, &len, &tmpbuf)) {
                                img->exif->addKeyVal(exiftag_list[i].tag_id, "Photo", tmpbuf, len);
                            }
                        }
                        else {
                            len = exiftag_list[i].len;
                            if (TIFFGetField (tif, exiftag_list[i].tag_id, &tmpbuf)) {
                                img->exif->addKeyVal(exiftag_list[i].tag_id, "Photo", tmpbuf, len);
                            }
                        }
                        break;
                    }
                    default: {
                        // NO ACTION HERE At THE MOMENT...
                    }
                }
            }
        }
        TIFFSetDirectory(tif, curdir);
    }
    //============================================================================


    void SipiIOTiff::writeExif(SipiImage *img, TIFF *tif)
    {
        // add EXIF tags to the set of tags that libtiff knows about
        // necessary if we want to set EXIFTAG_DATETIMEORIGINAL, for example
        //const TIFFFieldArray *exif_fields = _TIFFGetExifFields();
        //_TIFFMergeFields(tif, exif_fields->fields, exif_fields->count);


        TIFFCreateEXIFDirectory(tif);
        int count = 0;
        for (int i = 0; i < exiftag_list_len; i++) {
            switch (exiftag_list[i].datatype) {
                case EXIF_DT_RATIONAL: {
                    Exiv2::Rational r;
                    if (img->exif->getValByKey(exiftag_list[i].tag_id, "Photo", r)) {
                        float f = (float) r.first / (float) r.second;
                        TIFFSetField(tif, exiftag_list[i].tag_id, f);
                        count++;
                    }
                    break;
                }
                case EXIF_DT_UINT8: {
                    uint8 uc;
                    if (img->exif->getValByKey(exiftag_list[i].tag_id, "Photo", uc)) {
                        TIFFSetField(tif, exiftag_list[i].tag_id, uc);
                        count++;
                    }
                    break;
                }
                case EXIF_DT_UINT16: {
                    uint16 us;
                    if (img->exif->getValByKey(exiftag_list[i].tag_id, "Photo", us)) {
                        TIFFSetField(tif, exiftag_list[i].tag_id, us);
                        count++;
                    }
                    break;
                }
                case EXIF_DT_UINT32: {
                    uint32 ui;
                    if (img->exif->getValByKey(exiftag_list[i].tag_id, "Photo", ui)) {
                        TIFFSetField(tif, exiftag_list[i].tag_id, ui);
                        count++;
                    }
                    break;
                }
                case EXIF_DT_STRING: {
                    string tmpstr;
                    if (img->exif->getValByKey(exiftag_list[i].tag_id, "Photo", tmpstr)) {
                        TIFFSetField(tif, exiftag_list[i].tag_id, tmpstr.c_str());
                        count++;
                    }
                    break;
                }
                case EXIF_DT_RATIONAL_PTR: {
                    vector<Exiv2::Rational> vr;
                    if (img->exif->getValByKey(exiftag_list[i].tag_id, "Photo", vr)) {
                        int len = vr.size();
                        float *f = new float[len];
                        for (int i = 0; i < len; i++) {
                            f[i] = (float) vr[i].first / (float) vr[i].second; //!!!!!!!!!!!!!!!!!!!!!!!!!
                        }
                        TIFFSetField(tif, exiftag_list[i].tag_id, len, f);
                        delete [] f;
                        count++;
                    }
                    break;
                }
                case EXIF_DT_UINT8_PTR: {
                    vector<uint8> vuc;
                    if (img->exif->getValByKey(exiftag_list[i].tag_id, "Photo", vuc)) {
                        int len = vuc.size();
                        uint8 *uc = new uint8[len];
                        for (int i = 0; i < len; i++) {
                            uc[i] = vuc[i];
                        }
                        TIFFSetField(tif, exiftag_list[i].tag_id, len, uc);
                        delete [] uc;
                        count++;
                    }
                    break;
                }
                case EXIF_DT_UINT16_PTR: {
                    vector<uint16> vus;
                    if (img->exif->getValByKey(exiftag_list[i].tag_id, "Photo", vus)) {
                        int len = vus.size();
                        uint16 *us = new uint16[len];
                        for (int i = 0; i < len; i++) {
                            us[i] = vus[i];
                        }
                        TIFFSetField(tif, exiftag_list[i].tag_id, len, us);
                        delete [] us;
                        count++;
                    }
                    break;
                }
                case EXIF_DT_UINT32_PTR: {
                    vector<uint32> vui;
                    if (img->exif->getValByKey(exiftag_list[i].tag_id, "Photo", vui)) {
                        int len = vui.size();
                        uint32 *ui = new uint32[len];
                        for (int i = 0; i < len; i++) {
                            ui[i] = vui[i];
                        }
                        TIFFSetField(tif, exiftag_list[i].tag_id, len, ui);
                        delete [] ui;
                        count++;
                    }
                    break;
                }
                case EXIF_DT_PTR: {
                    vector<unsigned char> vuc;
                    if (img->exif->getValByKey(exiftag_list[i].tag_id, "Photo", vuc)) {
                        int len = vuc.size();
                        unsigned char *uc = new unsigned char[len];
                        for (int i = 0; i < len; i++) {
                            uc[i] = vuc[i];
                        }
                        TIFFSetField(tif, exiftag_list[i].tag_id, len, uc);
                        delete [] uc;
                        count++;
                    }
                    break;
                }
                default: {
                    // NO ACTION HERE At THE MOMENT...
                }
            }
        }

        if (count > 0) {
            uint64 exif_dir_offset = 0;
            TIFFWriteCustomDirectory(tif, &exif_dir_offset);
            TIFFSetDirectory(tif, 0);
            TIFFSetField(tif, TIFFTAG_EXIFIFD, exif_dir_offset);
        }
        //TIFFCheckpointDirectory(tif);
    }
    //============================================================================


    void SipiIOTiff::separateToContig(SipiImage *img, unsigned int sll) {
        //
        // rearrange RRRRRR...GGGGG...BBBBB data  to RGBRGBRGB…RGB
        //
        if (img->bps == 8) {
            byte *dataptr = (byte *) img->pixels;
            unsigned char *tmpptr = new unsigned char[img->nc*img->ny*img->nx];
            for (unsigned int k = 0; k < img->nc; k++) {
                for (unsigned int j = 0; j < img->ny; j++) {
                    for (unsigned int i = 0; i < img->nx; i++) {
                        tmpptr[img->nc*(j*img->nx + i) + k] = dataptr[k*img->ny*sll + j*img->nx + i];
                    }
                }
            }
            delete [] dataptr;
            img->pixels = tmpptr;
        }
        else if (img->bps == 16) {
            word *dataptr = (word *) img->pixels;
            word *tmpptr = new word[img->nc*img->ny*img->nx];
            for (unsigned int k = 0; k < img->nc; k++) {
                for (unsigned int j = 0; j < img->ny; j++) {
                    for (unsigned int i = 0; i < img->nx; i++) {
                        tmpptr[img->nc*(j*img->nx + i) + k] = dataptr[k*img->ny*sll + j*img->nx + i];
                    }
                }
            }
            delete [] (word *) dataptr;
            img->pixels = (byte *) tmpptr;
        }
        else  {
            string msg = "Bits per sample not supported: " + to_string(-img->bps);
            throw SipiError(__file__, __LINE__, msg);
        }
    }
    //============================================================================


    void SipiIOTiff::cvrt1BitTo8Bit(SipiImage *img, unsigned int sll, unsigned int black, unsigned int white) {
        byte *inbuf = img->pixels;
        byte *outbuf;
        byte *in_byte, *out_byte, *in_off, *out_off, *inbuf_high;

        static unsigned char mask[8] = {128,64,32,16,8,4,2,1};

        unsigned int x, y, k;

        if (img->bps != 1) {
            string msg = "Bits per sample is not 1 but: " + to_string(img->bps);
            throw SipiError(__file__, __LINE__, msg);
        }

        outbuf = new byte[img->nx*img->ny];

        inbuf_high = inbuf + img->ny * sll;

        if ((8*sll) == img->nx){
            in_byte  = inbuf;
            out_byte = outbuf;

            for (; in_byte < inbuf_high; in_byte++, out_byte += 8){
                for (k = 0; k < 8; k++) {
                    *(out_byte + k) = (*(mask + k) & *in_byte) ? white : black;
                }
            }
        }
        else{
            out_off = outbuf;
            in_off  = inbuf;

            for (y = 0; y < img->ny; y++, out_off += img->nx, in_off += sll){
                x = 0;
                for (in_byte = in_off; in_byte < in_off+sll; in_byte++, x += 8){
                    out_byte = out_off + x;

                    if ((x+8) <= img->nx){
                        for ( k = 0; k < 8; k++){
                            *(out_byte + k) = (*(mask + k) & *in_byte) ? white : black;
                        }
                    }
                    else{
                        for ( k = 0; (x+k) < img->nx; k++){
                            *(out_byte + k) = (*(mask + k) & *in_byte) ? white : black;
                        }
                    }
                }
            }
        }
        img->pixels = outbuf;
        delete [] inbuf;
        img->bps = 8;
    }
    //============================================================================

}
