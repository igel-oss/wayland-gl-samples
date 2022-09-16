/*
 * Copyright © 2022 IGEL Co., Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Tomohito Esaki <etom@igel.co.jp>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <assert.h>
#include <cairo.h>

#include <GLES3/gl3.h>

#include "common.h"
#include "shader.h"

#define WINDOW_WIDTH		400
#define WINDOW_HEIGHT		400

#define TEX_WIDTH               256
#define TEX_HEIGHT              256

struct gl_info {
	GLuint sampler;
	GLuint sh_position;
	GLuint sh_color;
	GLuint sh_texture;
	GLuint texture;
	GLuint vao;
	GLuint buffers[2];
	GLuint program;
};

#define N_BALL		1000

#define TO_STRING(x)	#x
static const char *vert_shader_text = "#version 300 es\n" TO_STRING(
	in vec3 position;
	in vec4 color;
	out lowp vec4 vColor;

	void main()
	{
		gl_Position = vec4(position, 1.0);

		vColor = color;
		gl_PointSize = 10.0;
	});

static const char *frag_shader_text = "#version 300 es\n" TO_STRING(
	in lowp vec4 vColor;
	out mediump vec4 fragColor;
	uniform mediump sampler2D tex;
	void main() {
		fragColor = texture(tex, gl_PointCoord).bgra * vColor;
	});

static GLuint
create_texture()
{
	cairo_surface_t *img;
	cairo_t *cr;
	unsigned char *data;
	GLuint texture;

	img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
					 TEX_WIDTH, TEX_HEIGHT);
	cr = cairo_create(img);
	cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
	cairo_arc(cr, TEX_WIDTH / 2, TEX_HEIGHT / 2, TEX_WIDTH / 2, 0,
		  2 * M_PI);
	cairo_fill(cr);
	cairo_stroke(cr);
	cairo_surface_flush(img);
	cairo_destroy(cr);

	data = cairo_image_surface_get_data(img);
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX_WIDTH, TEX_HEIGHT, 0,
		     GL_RGBA, GL_UNSIGNED_BYTE, data);

	cairo_surface_destroy(img);

	return texture;
}

static void
init_gl_buffer(struct gl_info *gl)
{
	static GLfloat vertex[N_BALL * 3];
	static GLfloat color[N_BALL * 4];
	int i;

	srand(time(NULL));
	for (i = 0; i < N_BALL; i++) {
		vertex[3 * i] = (rand() % 2000 - 1000) / 1000.0;
		vertex[3 * i + 1] = (rand() % 2000 - 1000) / 1000.0;
		vertex[3 * i + 2] = (rand() % 2000 - 1000) / 1000.0;
		color[4 * i] = rand() % 1000 / 1000.0;
		color[4 * i + 1] = rand() % 1000 / 1000.0;
		color[4 * i + 2] = rand() % 1000 / 1000.0;
		color[4 * i + 3] = (rand() % 500 + 500) / 1000.0;
	}

	glGenVertexArrays(1, &gl->vao);
	glBindVertexArray(gl->vao);

	glGenBuffers(2, gl->buffers);

	glBindBuffer(GL_ARRAY_BUFFER, gl->buffers[0]);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertex), vertex,
		     GL_STATIC_DRAW);
	glVertexAttribPointer(gl->sh_position, 3, GL_FLOAT,
			      GL_FALSE, 0, 0);
	glVertexAttribDivisor(gl->sh_position, 1);

	glBindBuffer(GL_ARRAY_BUFFER, gl->buffers[1]);
	glBufferData(GL_ARRAY_BUFFER, sizeof(color), color,
		     GL_STATIC_DRAW);
	glVertexAttribPointer(gl->sh_color, 4, GL_FLOAT, GL_FALSE,
			      0, 0);
	glVertexAttribDivisor(gl->sh_color, 1);

	glEnableVertexAttribArray(gl->sh_position);
	glEnableVertexAttribArray(gl->sh_color);

	glBindVertexArray(0);
}

static void
init_gl(void *data)
{
	struct gl_info *gl = data;
	struct shader_info shader;

	memset(&shader, 0x0, sizeof(shader));
	shader.vertex = vert_shader_text;
	shader.fragment = frag_shader_text;
	gl->program = shader_build_program(&shader);
	assert(gl->program);
	glUseProgram(gl->program);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	gl->sh_position = glGetAttribLocation(gl->program, "position");
	gl->sh_color = glGetAttribLocation(gl->program, "color");
	gl->sh_texture = glGetUniformLocation(gl->program, "tex");
	glUniform1i(gl->sh_texture, 0);

	gl->texture = create_texture();
	glGenSamplers(1, &gl->sampler);
	glSamplerParameteri(gl->sampler, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glSamplerParameteri(gl->sampler, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glSamplerParameteri(gl->sampler, GL_TEXTURE_MIN_FILTER,
			    GL_LINEAR);
	glSamplerParameteri(gl->sampler, GL_TEXTURE_MAG_FILTER,
			    GL_LINEAR);

	init_gl_buffer(gl);
}

static void
deinit_gl(void *data)
{
	struct gl_info *gl = data;

	glDeleteSamplers(1, &gl->sampler);
	glDeleteVertexArrays(1, &gl->vao);
	glDeleteBuffers(2, gl->buffers);
	glDeleteTextures(1, &gl->texture);
	glDeleteProgram(gl->program);
}

static void
redraw(void *data, struct rect *damage)
{
	struct gl_info *gl = data;

	glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
	glClearColor(0.0, 0.0, 0.0, 0.5);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, gl->texture);
	glBindSampler(0, gl->sampler);

	glBindVertexArray(gl->vao);
	glDrawArraysInstanced(GL_POINTS, 0, 1, N_BALL);
	glBindVertexArray(0);

	{
		static int fps_count = 0;
		static long long fps_time = 0;
		static float fps;
		struct timespec tm;
		long long now;

		clock_gettime(CLOCK_MONOTONIC, &tm);
		now = tm.tv_sec * 1000LL + tm.tv_nsec / 1000000LL;
		fps_count++;
		if (!fps_time) {
			fps_time = now;
		} else {
			long long diff = now - fps_time;
			if (diff >= 1000) {
				fps = fps_count * 1000 / (float)diff;
				fps_time = now;
				fprintf(stderr, "FPS: %.2f (%d/%.2fsec)\n",
					fps, fps_count, diff / 1000.0);
				fps_count = 0;
			}
		}
	}
}


int
main(int argc, char **argv)
{
	struct gl_info gl;
	struct app_info app = {
		.name = "gl-instanced-rendering3",
		.id = "jp.co.igel.gl-instanced-rendering3",
		.win_width = WINDOW_WIDTH,
		.win_height = WINDOW_HEIGHT,
		.cb = {
			.init_gl = init_gl,
			.deinit_gl = deinit_gl,
			.redraw = redraw,
			.user_data = &gl,
		},
	};

	app_main(argc, argv, &app);

	return 0;
}
