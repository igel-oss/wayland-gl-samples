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

#include <string.h>
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
	GLuint sh_trans;
	GLuint texture;
	GLuint vao;
	GLuint buffers[2];
	GLuint program;
};

#define MAX_INSTANCE_COUNT	4

#define TO_STRING(x)	#x
static const char *vert_shader_text = "#version 300 es\n" TO_STRING(
	in vec3 position;
	in vec4 color;
	out lowp vec4 vColor;
	uniform vec2 translate[4];

	void main()
	{
		mat4 model = mat4(
			1.0, 0.0, 0.0, 0.0,
			0.0, 1.0, 0.0, 0.0,
			0.0, 0.0, 1.0, 0.0,
			translate[gl_InstanceID].x, translate[gl_InstanceID].y, 0.0, 1.0);
		gl_Position = model * vec4(position, 1.0);

		vColor = color;
		gl_PointSize = 20.0;
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

static void init_gl_buffer(struct gl_info *gl)
{
	static const GLfloat vertex[] = {
		0.0, 0.0, 0.0,
	};
	static const GLfloat color[] = {
		0.0, 0.0, 1.0, 1.0,
	};

	glGenVertexArrays(1, &gl->vao);
	glBindVertexArray(gl->vao);

	glGenBuffers(2, gl->buffers);

	glBindBuffer(GL_ARRAY_BUFFER, gl->buffers[0]);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertex), vertex,
		     GL_STREAM_DRAW);
	glVertexAttribPointer(gl->sh_position, 3, GL_FLOAT,
			      GL_FALSE, 0, 0);

	glBindBuffer(GL_ARRAY_BUFFER, gl->buffers[1]);
	glBufferData(GL_ARRAY_BUFFER, sizeof(color), color,
		     GL_STATIC_DRAW);
	glVertexAttribPointer(gl->sh_color, 4, GL_FLOAT, GL_FALSE,
			      0, 0);

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
	gl->sh_trans =
		glGetUniformLocation(gl->program, "translate");
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
	GLfloat trans[] = {
		-0.8, -0.8,
		-0.4, -0.4,
		0.4, 0.4,
		0.8, 0.8,
	};

	glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
	glClearColor(0.0, 0.0, 0.0, 0.5);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, gl->texture);
	glBindSampler(0, gl->sampler);

	glBindVertexArray(gl->vao);
	glUniform2fv(gl->sh_trans, MAX_INSTANCE_COUNT, trans);
	glDrawArraysInstanced(GL_POINTS, 0, 1, MAX_INSTANCE_COUNT);
	glBindVertexArray(0);
}

int
main(int argc, char **argv)
{
	struct gl_info gl;
	struct app_info app = {
		.name = "gl-instanced-rendering1",
		.id = "jp.co.igel.gl-instanced-rendering1",
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
