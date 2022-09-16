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
#include <math.h>
#include <assert.h>
#include <time.h>
#include <cairo.h>

#include <GLES3/gl31.h>

#include "common.h"
#include "shader.h"

#define WINDOW_WIDTH		1920
#define WINDOW_HEIGHT		1080

struct gl_info {
	struct {
		GLuint render_fbo;
		GLuint render_screen;
		GLuint compute;
	} program;
	struct {
		struct {
			GLuint vertex;
			GLuint color;
			GLuint ball;
		} fbo;
		struct {
			GLuint vertex;
			GLuint texcoord;
			GLuint index;
		} screen;
		struct {
			GLuint vertex;
			GLuint texcoord;
		} frame;
	} buffer;
	GLuint texture;
	GLuint fbo;
	GLuint rb;
	GLuint tex_frame;
};

#define N_BALL		1000000
struct ball {
	float x;
	float y;
	float vx;
	float vy;
};

enum {
	GL_SH_LOC_FBO_POSITION = 0,
	GL_SH_LOC_FBO_COLOR,
	GL_SH_LOC_SC_POSITION,
	GL_SH_LOC_SC_TEXCOORD,
	GL_SH_LOC_SC_SRC_TEX,
};

enum {
	GL_SH_BINDING_VERTEX = 1,
	GL_SH_BINDING_BALLDATA,
};

#define TO_STRING(x)	#x
static const char *fbo_vshader_code = "#version 310 es\n" TO_STRING(
	layout (location=0) in vec4 position;
	layout (location=1) in vec4 color;
	out vec4 vColor;
	void main()
	{
		gl_Position = position;
		gl_PointSize = 10.0;
		vColor = color;
	});

static const char *fbo_fshader_code = "#version 310 es\n" TO_STRING(
	out mediump vec4 fragColor;
	in mediump vec4 vColor;
	void main()
	{
		mediump vec3 n;
		n.xy = gl_PointCoord * 2.0 - 1.0;
		n.z = 1.0 - dot(n.xy, n.xy);
		if (n.z < 0.0) discard;

		fragColor = vColor * n.z;
	});

static const char *vshader_code = "#version 310 es\n" TO_STRING(
	layout (location=2) in vec2 position;
	layout (location=3) in vec2 texcoord;
	out vec2 vTexcoord;
	void main()
	{
		gl_Position = vec4(position.xy, 0.0, 1.0);
		vTexcoord = texcoord;
	});

static const char *fshader_code = "#version 310 es\n" TO_STRING(
	out mediump vec4 fragColor;
	in mediump vec2 vTexcoord;
	layout (location=4) uniform sampler2D srcTex;
	void main()
	{
		fragColor = texture(srcTex, vTexcoord);
	});

static const char *cshader_code = "#version 310 es\n" TO_STRING(
	struct BallObj {
		vec2 p;
		vec2 v;
	};
	layout (std140, binding=1) buffer vertex {
		vec4 pos[];
	};
	layout (std140, binding=2) buffer balls {
		BallObj b[];
	};
	layout (local_size_x = 32) in;
	void main()
	{
		float th = 0.985;
                uint i = gl_GlobalInvocationID.x;
                b[i].p += b[i].v;
                if (b[i].p.x < -th) {
                        b[i].p.x = -th;
                        b[i].v.x = -b[i].v.x;
                }
                if (b[i].p.x > th) {
                        b[i].p.x = th;
                        b[i].v.x = -b[i].v.x;
                }
                if (b[i].p.y < -th) {
                        b[i].p.y = -th;
                        b[i].v.y = -b[i].v.y;
                }
                if (b[i].p.y > th) {
                        b[i].p.y = th;
                        b[i].v.y = -b[i].v.y;
                }
                pos[i] = vec4(b[i].p.xy, 0.0, 1.0);
	});

static void
init_texture(struct gl_info *gl)
{
	GLuint texture;

	glGenTextures(1, &texture);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, WINDOW_WIDTH, WINDOW_HEIGHT, 0,
		     GL_RGBA, GL_UNSIGNED_BYTE, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	gl->texture = texture;

	glGenTextures(1, &texture);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, WINDOW_WIDTH, WINDOW_HEIGHT, 0,
		     GL_RGBA, GL_UNSIGNED_BYTE, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	gl->tex_frame = texture;
}

static void
init_shader(struct gl_info *gl)
{
	struct shader_info shader;
	GLuint program;

	memset(&shader, 0x0, sizeof(shader));
	shader.vertex = fbo_vshader_code;
	shader.fragment = fbo_fshader_code;
	program = shader_build_program(&shader);
	assert(program);
	gl->program.render_fbo = program;

	memset(&shader, 0x0, sizeof(shader));
	shader.vertex = vshader_code;
	shader.fragment = fshader_code;
	program = shader_build_program(&shader);
	assert(program);
	gl->program.render_screen = program;

	memset(&shader, 0x0, sizeof(shader));
	shader.compute = cshader_code;
	program = shader_build_program(&shader);
	assert(program);
	gl->program.compute = program;

	glEnableVertexAttribArray(GL_SH_LOC_FBO_POSITION);
	glEnableVertexAttribArray(GL_SH_LOC_FBO_COLOR);
	glEnableVertexAttribArray(GL_SH_LOC_SC_POSITION);
	glEnableVertexAttribArray(GL_SH_LOC_SC_TEXCOORD);
}

static void
init_fbo(struct gl_info *gl)
{
	glGenFramebuffers(1, &gl->fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, gl->fbo);
	glGenRenderbuffers(1, &gl->rb);
	glBindRenderbuffer(GL_RENDERBUFFER, gl->rb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
			      WINDOW_WIDTH, WINDOW_HEIGHT);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
				  GL_RENDERBUFFER, gl->rb);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
			       GL_TEXTURE_2D, gl->texture, 0);
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void
init_buffer(struct gl_info *gl)
{
	GLfloat vertex[] = {
		-1, -1,
		-1, 1,
		1, 1,
		1, -1,
	};
	GLfloat texcoord[] = {
		0, 0,
		0, 1,
		1, 1,
		1, 0,
	};
	GLushort index[] = {
		0, 1, 2, 0, 2, 3
	};
	float *color;
	struct ball *b;
	float sp, ang;
	int i;

	color = calloc(N_BALL * 4, sizeof(float));
	b = calloc(N_BALL, sizeof(struct ball));
	srand(time(NULL));
	for (i = 0; i < N_BALL; i++) {
		b[i].x = (rand() % 1000) / 500.0 - 1.0;
		b[i].y = (rand() % 1000) / 500.0 - 1.0;
		sp = rand() % 200 / 10000.0 + 0.01;
		ang = rand() % 360 / 180.0 * M_PI;
		b[i].vx = sp * cos(ang);
		b[i].vy = sp * sin(ang);
		switch (rand() % 7) {
		case 0:
			color[i * 4] = 1;
			color[i * 4 + 1] = 1;
			color[i * 4 + 2] = 1;
			break;
		case 1:
			color[i * 4] = 1;
			break;
		case 2:
			color[i * 4] = 1;
			color[i * 4 + 2] = 1;
			break;
		case 3:
			color[i * 4 + 2] = 1;
			break;
		case 4:
			color[i * 4 + 1] = 1;
			color[i * 4 + 2] = 1;
			break;
		case 5:
			color[i * 4 + 1] = 1;
			break;
		case 6:
			color[i * 4] = 1;
			color[i * 4 + 1] = 1;
			break;
		}
		color[i * 4 + 3] = 1;
	}

	glGenBuffers(1, &gl->buffer.fbo.vertex);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl->buffer.fbo.vertex);
	glBufferData(GL_SHADER_STORAGE_BUFFER, N_BALL * 4 * sizeof(GLfloat), 0,
		     GL_STATIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, GL_SH_BINDING_VERTEX,
			 gl->buffer.fbo.vertex);
	glGenBuffers(1, &gl->buffer.fbo.ball);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl->buffer.fbo.ball);
	glBufferData(GL_SHADER_STORAGE_BUFFER, N_BALL * sizeof(struct ball), b,
		     GL_STATIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, GL_SH_BINDING_BALLDATA,
			 gl->buffer.fbo.ball);

	glUseProgram(gl->program.render_fbo);
	glBindBuffer(GL_ARRAY_BUFFER, gl->buffer.fbo.vertex);
	glVertexAttribPointer(GL_SH_LOC_FBO_POSITION, 4, GL_FLOAT, GL_FALSE, 0,
			      0);
	glGenBuffers(1, &gl->buffer.fbo.color);
	glBindBuffer(GL_ARRAY_BUFFER, gl->buffer.fbo.color);
	glBufferData(GL_ARRAY_BUFFER, N_BALL * 4 * sizeof(GLfloat), color,
		     GL_STATIC_DRAW);
	glVertexAttribPointer(GL_SH_LOC_FBO_COLOR, 4, GL_FLOAT, GL_FALSE, 0, 0);

	glGenBuffers(1, &gl->buffer.screen.vertex);
	glBindBuffer(GL_ARRAY_BUFFER, gl->buffer.screen.vertex);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertex), vertex, GL_STATIC_DRAW);
	glVertexAttribPointer(GL_SH_LOC_SC_POSITION, 2, GL_FLOAT, GL_FALSE, 0,
			      0);
	glGenBuffers(1, &gl->buffer.screen.texcoord);
	glBindBuffer(GL_ARRAY_BUFFER, gl->buffer.screen.texcoord);
	glBufferData(GL_ARRAY_BUFFER, sizeof(texcoord), texcoord,
		     GL_STATIC_DRAW);
	glVertexAttribPointer(GL_SH_LOC_SC_TEXCOORD, 2, GL_FLOAT, GL_FALSE, 0,
			      0);
	glGenBuffers(1, &gl->buffer.screen.index);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gl->buffer.screen.index);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(index), index,
		     GL_STATIC_DRAW);
	free(b);
	free(color);

	glGenBuffers(1, &gl->buffer.frame.vertex);
	glGenBuffers(1, &gl->buffer.frame.texcoord);
}

static void
init_gl(void *data)
{
	struct gl_info *gl = data;

	init_texture(gl);
	init_shader(gl);
	init_fbo(gl);
	init_buffer(gl);
}

static void
deinit_gl(void *data)
{
	struct gl_info *gl = data;

	glDeleteFramebuffers(1, &gl->fbo);
	glDeleteRenderbuffers(1, &gl->rb);
	glDeleteBuffers(1, &gl->buffer.fbo.vertex);
	glDeleteBuffers(1, &gl->buffer.fbo.ball);
	glDeleteBuffers(1, &gl->buffer.fbo.color);
	glDeleteBuffers(1, &gl->buffer.screen.vertex);
	glDeleteBuffers(1, &gl->buffer.screen.texcoord);
	glDeleteBuffers(1, &gl->buffer.screen.index);
	glDeleteBuffers(1, &gl->buffer.frame.vertex);
	glDeleteBuffers(1, &gl->buffer.frame.texcoord);
	glDeleteTextures(1, &gl->texture);
	glDeleteTextures(1, &gl->tex_frame);
	glDeleteProgram(gl->program.render_fbo);
	glDeleteProgram(gl->program.render_screen);
	glDeleteProgram(gl->program.compute);
}

static void
draw_frame_count(struct gl_info *gl, int frame)
{
	cairo_surface_t *surface;
	cairo_t *cr;
	cairo_text_extents_t ext;
	char str[16];
	uint8_t *data;

	snprintf(str, sizeof(str), "%d", frame);

	surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 64);
	cr = cairo_create(surface);
	cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
	cairo_rectangle(cr, 0, 0, 64, 64);
        cairo_fill(cr);
	cairo_set_font_size(cr, 30.0);
	cairo_text_extents(cr, str, &ext);
	cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
	cairo_move_to(cr, 0, ext.height);
	cairo_show_text(cr, str);
	data = cairo_image_surface_get_data(surface);

	glBindTexture(GL_TEXTURE_2D, gl->tex_frame);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA,
		     GL_UNSIGNED_BYTE, data);

	cairo_destroy(cr);
	cairo_surface_destroy(surface);

	GLfloat vertex[] = {
		0.8, 1,
		1, 1,
		1, 0.8,
		0.8, 0.8,
		0.8, 1,
	};
	GLfloat texcoord[] = {
		0, 0,
		1, 0,
		1, 1,
		0, 1,
		0, 0,
	};

	glUniform1i(GL_SH_LOC_SC_SRC_TEX, 1);
	glBindBuffer(GL_ARRAY_BUFFER, gl->buffer.frame.vertex);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertex), vertex, GL_STATIC_DRAW);
	glVertexAttribPointer(GL_SH_LOC_SC_POSITION, 2, GL_FLOAT, GL_FALSE, 0,
			      0);
	glBindBuffer(GL_ARRAY_BUFFER, gl->buffer.frame.texcoord);
	glBufferData(GL_ARRAY_BUFFER, sizeof(texcoord), texcoord,
		     GL_STATIC_DRAW);
	glVertexAttribPointer(GL_SH_LOC_SC_TEXCOORD, 2, GL_FLOAT, GL_FALSE, 0,
			      0);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 5);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static void
redraw(void *data, struct rect *damage)
{
	struct gl_info *gl = data;
	static int frame = 0;

	glUseProgram(gl->program.compute);
	glDispatchCompute(N_BALL / 32, 1, 1);

	glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);

	//draw to FBO
	glUseProgram(gl->program.render_fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, gl->fbo);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glClearColor(0.0f, 0.0f, 0.0f, 0.5f);
	glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_POINTS, 0, N_BALL);

	//draw to Screen
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glUseProgram(gl->program.render_screen);
	glDisable(GL_BLEND);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glUniform1i(GL_SH_LOC_SC_SRC_TEX, 0);
	glBindBuffer(GL_ARRAY_BUFFER, gl->buffer.screen.vertex);
	glVertexAttribPointer(GL_SH_LOC_SC_POSITION, 2, GL_FLOAT, GL_FALSE, 0, 0);
	glBindBuffer(GL_ARRAY_BUFFER, gl->buffer.screen.texcoord);
	glVertexAttribPointer(GL_SH_LOC_SC_TEXCOORD, 2, GL_FLOAT, GL_FALSE, 0, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gl->buffer.screen.index);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

	frame++;
	draw_frame_count(gl, frame);
}

int
main(int argc, char **argv)
{
	struct gl_info gl;
	struct app_info app = {
		.name = "gl-compute3",
		.id = "jp.co.igel.gl-compute3",
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
