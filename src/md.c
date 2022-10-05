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

  /* El size de entrada es una buena aproximación del size de salida. Se
   * reserva un poco más para almacenar el header y el footer. */
  membuf_init(&buf_out, (MD_SIZE)(buf_in.size + buf_in.size/8 + 64));

  /* Analizar el documento. Esto debería llamar a nuestro callback
   * proporcionado por la estructura md_renderer_t.
   */

  ret = md_html(buf_in.data, buf_in.size, process_output, (void*) &buf_out,
          parser_flags, renderer_flags);

  if(ret != 0)
      fprintf(stderr, "Parsing failed.\n");
  membuf_fini(&buf_in);
  return buf_out;
}



static const char* input_path = NULL;
static const char* output_path = NULL;


/* Recibe un nombre de archivo markdown y retorna
 * el código convertido a html
 */

char *md_to_html(FILE *file)
{
  struct membuffer ret = {0};

  if (!file) {
    return "md_to_html() No existe el archivo\n";
  }

  ret = process_file(file);
  return ret.data;
}
