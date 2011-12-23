#include <jni.h>
#include <time.h>
#include <android/log.h>
#include <android/bitmap.h>

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "fitz.h"
#include "mupdf.h"

#define LOG_TAG "libmupdf"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)

/* Set to 1 to enable debug log traces. */
#define DEBUG 0

/* Enable to log rendering times (render each frame 100 times and time) */
#undef TIME_DISPLAY_LIST

/* Globals */
fz_colorspace *colorspace;
fz_glyph_cache *glyphcache;
pdf_xref *xref;
int pagenum = 1;
int resolution = 160;
float pageWidth = 100;
float pageHeight = 100;
fz_display_list *currentPageList;
fz_rect currentMediabox;
int currentRotate;
fz_context *ctx;

JNIEXPORT int JNICALL
Java_com_artifex_mupdf_MuPDFCore_openFile(JNIEnv * env, jobject thiz, jstring jfilename)
{
	const char *filename;
	char *password = "";
	int accelerate = 1;
	int pages = 0;

	filename = (*env)->GetStringUTFChars(env, jfilename, NULL);
	if (filename == NULL)
	{
		LOGE("Failed to get filename");
		return 0;
	}

	if (accelerate)
		fz_accelerate();

	ctx = fz_new_context(&fz_alloc_default, FZ_STORE_UNLIMITED);
	if (!ctx)
	{
		LOGE("Failed to initialise context");
		return 0;
	}

	xref = NULL;
	glyphcache = NULL;
	fz_try(ctx)
	{
		glyphcache = fz_new_glyph_cache(ctx);
		colorspace = fz_device_rgb;

		LOGE("Opening document...");
		fz_try(ctx)
		{
			xref = pdf_open_xref(ctx, filename, password);
		}
		fz_catch(ctx)
		{
			fz_throw(ctx, "Cannot open document: '%s'\n", filename);
		}

		LOGE("Loading page tree...");
		fz_try(ctx)
		{
			pdf_load_page_tree(xref);
		}
		fz_catch(ctx)
		{
			fz_throw(ctx, "Cannot load page tree: '%s'\n", filename);
		}
		pages = pdf_count_pages(xref);
		LOGE("Done! %d pages", pages);
	}
	fz_catch(ctx)
	{
		LOGE("Failed: %s", ctx->error->message);
		pdf_free_xref(xref);
		xref = NULL;
		fz_free_glyph_cache(ctx, glyphcache);
		glyphcache = NULL;
		fz_free_context(ctx);
		ctx = NULL;
	}

	return pages;
}

JNIEXPORT void JNICALL
Java_com_artifex_mupdf_MuPDFCore_gotoPageInternal(JNIEnv *env, jobject thiz, int page)
{
	float zoom;
	fz_matrix ctm;
	fz_bbox bbox;
	fz_device *dev = NULL;
	pdf_page *currentPage = NULL;

	fz_var(dev);
	fz_var(currentPage);

	/* In the event of an error, ensure we give a non-empty page */
	pageWidth = 100;
	pageHeight = 100;

	LOGE("Goto page %d...", page);
	fz_try(ctx)
	{
		if (currentPageList != NULL)
		{
			fz_free_display_list(ctx, currentPageList);
			currentPageList = NULL;
		}
		pagenum = page;
		currentPage = pdf_load_page(xref, pagenum);
		zoom = resolution / 72;
		currentMediabox = currentPage->mediabox;
		currentRotate = currentPage->rotate;
		ctm = fz_translate(0, -currentMediabox.y1);
		ctm = fz_concat(ctm, fz_scale(zoom, -zoom));
		ctm = fz_concat(ctm, fz_rotate(currentRotate));
		bbox = fz_round_rect(fz_transform_rect(ctm, currentMediabox));
		pageWidth = bbox.x1-bbox.x0;
		pageHeight = bbox.y1-bbox.y0;
		/* Render to list */
		currentPageList = fz_new_display_list(ctx);
		dev = fz_new_list_device(ctx, currentPageList);
		pdf_run_page(xref, currentPage, dev, fz_identity);
	}
	fz_catch(ctx)
	{
		LOGE("cannot make displaylist from page %d", pagenum);
	}
	pdf_free_page(ctx, currentPage);
	currentPage = NULL;
	fz_free_device(dev);
	dev = NULL;
}

JNIEXPORT float JNICALL
Java_com_artifex_mupdf_MuPDFCore_getPageWidth(JNIEnv *env, jobject thiz)
{
	LOGE("PageWidth=%g", pageWidth);
	return pageWidth;
}

JNIEXPORT float JNICALL
Java_com_artifex_mupdf_MuPDFCore_getPageHeight(JNIEnv *env, jobject thiz)
{
	LOGE("PageHeight=%g", pageHeight);
	return pageHeight;
}

JNIEXPORT jboolean JNICALL
Java_com_artifex_mupdf_MuPDFCore_drawPage(JNIEnv *env, jobject thiz, jobject bitmap,
		int pageW, int pageH, int patchX, int patchY, int patchW, int patchH)
{
	AndroidBitmapInfo info;
	void *pixels;
	int ret;
	fz_device *dev = NULL;
	float zoom;
	fz_matrix ctm;
	fz_bbox bbox;
	fz_pixmap *pix = NULL;
	float xscale, yscale;
	fz_bbox rect;

	fz_var(pix);
	fz_var(dev);

	LOGI("In native method\n");
	if ((ret = AndroidBitmap_getInfo(env, bitmap, &info)) < 0) {
		LOGE("AndroidBitmap_getInfo() failed ! error=%d", ret);
		return 0;
	}

	LOGI("Checking format\n");
	if (info.format != ANDROID_BITMAP_FORMAT_RGBA_8888) {
		LOGE("Bitmap format is not RGBA_8888 !");
		return 0;
	}

	LOGI("locking pixels\n");
	if ((ret = AndroidBitmap_lockPixels(env, bitmap, &pixels)) < 0) {
		LOGE("AndroidBitmap_lockPixels() failed ! error=%d", ret);
		return 0;
	}

	/* Call mupdf to render display list to screen */
	LOGE("Rendering page=%dx%d patch=[%d,%d,%d,%d]",
			pageW, pageH, patchX, patchY, patchW, patchH);

	fz_try(ctx)
	{
		rect.x0 = patchX;
		rect.y0 = patchY;
		rect.x1 = patchX + patchW;
		rect.y1 = patchY + patchH;
		pix = fz_new_pixmap_with_rect_and_data(ctx, colorspace, rect, pixels);
		if (currentPageList == NULL)
		{
			fz_clear_pixmap_with_color(pix, 0xd0);
			break;
		}
		fz_clear_pixmap_with_color(pix, 0xff);

		zoom = resolution / 72;
		ctm = fz_translate(-currentMediabox.x0, -currentMediabox.y1);
		ctm = fz_concat(ctm, fz_scale(zoom, -zoom));
		ctm = fz_concat(ctm, fz_rotate(currentRotate));
		bbox = fz_round_rect(fz_transform_rect(ctm,currentMediabox));
		/* Now, adjust ctm so that it would give the correct page width
		 * heights. */
		xscale = (float)pageW/(float)(bbox.x1-bbox.x0);
		yscale = (float)pageH/(float)(bbox.y1-bbox.y0);
		ctm = fz_concat(ctm, fz_scale(xscale, yscale));
		bbox = fz_round_rect(fz_transform_rect(ctm,currentMediabox));
		dev = fz_new_draw_device(ctx, glyphcache, pix);
#ifdef TIME_DISPLAY_LIST
		{
			clock_t time;
			int i;

			LOGE("Executing display list");
			time = clock();
			for (i=0; i<100;i++) {
#endif
				fz_execute_display_list(currentPageList, dev, ctm, bbox);
#ifdef TIME_DISPLAY_LIST
			}
			time = clock() - time;
			LOGE("100 renders in %d (%d per sec)", time, CLOCKS_PER_SEC);
		}
#endif
		fz_free_device(dev);
		dev = NULL;
		fz_drop_pixmap(ctx, pix);
		LOGE("Rendered");
	}
	fz_catch(ctx)
	{
		fz_free_device(dev);
		LOGE("Render failed");
	}

	AndroidBitmap_unlockPixels(env, bitmap);

	return 1;
}

JNIEXPORT void JNICALL
Java_com_artifex_mupdf_MuPDFCore_destroying(JNIEnv * env, jobject thiz)
{
	fz_free_display_list(ctx, currentPageList);
	currentPageList = NULL;
	pdf_free_xref(xref);
	xref = NULL;
	fz_free_glyph_cache(ctx, glyphcache);
	glyphcache = NULL;
}
