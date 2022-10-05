/*
 * MD4C: Markdown parser for C
 * (http://github.com/mity/md4c)
 *
 * Copyright (c) 2016-2020 Martin Mitas
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "membuf.h"

#include "md4c-html.h"
#include "md4c.h"



/* Global options. */
static unsigned parser_flags = 0;
#ifndef MD4C_USE_ASCII
static unsigned renderer_flags = MD_HTML_FLAG_DEBUG | MD_HTML_FLAG_SKIP_UTF8_BOM;
#else
static unsigned renderer_flags = MD_HTML_FLAG_DEBUG;
#endif
static int want_fullhtml = 0;
static int want_xhtml = 0;
static int want_stat = 0;




/**********************
 ***  Main program  ***
 **********************/

  static void
process_output(const MD_CHAR* text, MD_SIZE size, void* userdata)
{
  membuf_append((struct membuffer*) userdata, text, size);
}

static struct membuffer process_file(FILE* in)
{
  size_t n;
  struct membuffer buf_in = {0};
  struct membuffer buf_out = {0};
  int ret = -1;
  clock_t t0, t1;

  membuf_init(&buf_in, 32 * 1024);

  /* Read the input file into a buffer. */
  while(1) {
    if(buf_in.size >= buf_in.asize)
      membuf_grow(&buf_in, buf_in.asize + buf_in.asize / 2);

    n = fread(buf_in.data + buf_in.size, 1, buf_in.asize - buf_in.size, in);
    if(n == 0)
      break;
    buf_in.size += n;
  }

  /* Input size is good estimation of output size. Add some more reserve to
   * deal with the HTML header/footer and tags. */
  membuf_init(&buf_out, (MD_SIZE)(buf_in.size + buf_in.size/8 + 64));

  /* Parse the document. This shall call our callbacks provided via the
   * md_renderer_t structure. */
  t0 = clock();

  ret = md_html(buf_in.data, (MD_SIZE)buf_in.size, process_output, (void*) &buf_out,
      parser_flags, renderer_flags);

  t1 = clock();
  if(ret != 0) {
    fprintf(stderr, "Parsing failed.\n");
    goto out;
  }

  /*
  // Write down the document in the HTML format. 
  if(want_fullhtml) {
    if(want_xhtml) {
      fprintf(out, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
      fprintf(out, "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.1//EN\" "
          "\"http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd\">\n");
      fprintf(out, "<html xmlns=\"http://www.w3.org/1999/xhtml\">\n");
    } else {
      fprintf(out, "<!DOCTYPE html>\n");
      fprintf(out, "<html>\n");
    }
    fprintf(out, "<head>\n");
    fprintf(out, "<title></title>\n");
    fprintf(out, "<meta name=\"generator\" content=\"md2html\"%s>\n", want_xhtml ? " /" : "");
    fprintf(out, "</head>\n");
    fprintf(out, "<body>\n");
  }

  fwrite(buf_out.data, 1, buf_out.size, out);

  if(want_fullhtml) {
    fprintf(out, "</body>\n");
    fprintf(out, "</html>\n");
  }

  if(want_stat) {
    if(t0 != (clock_t)-1  &&  t1 != (clock_t)-1) {
      double elapsed = (double)(t1 - t0) / CLOCKS_PER_SEC;
      if (elapsed < 1)
        fprintf(stderr, "Time spent on parsing: %7.2f ms.\n", elapsed*1e3);
      else
        fprintf(stderr, "Time spent on parsing: %6.3f s.\n", elapsed);
    }
  }
  */

  /* Success if we have reached here. */
  ret = 0;

out:
  membuf_fini(&buf_in);

  return buf_out;
}



static const char* input_path = NULL;
static const char* output_path = NULL;


/* Recibe un nombre de archivo markdown y retorna
 * el código convertido a html
 */

char *mdToHtml(FILE *file)
{
  struct membuffer ret = {0};

  if (!file) {
    return "mdToHtml() No existe el archivo\n";
  }

  ret = process_file(file);
  return ret.data;
}
