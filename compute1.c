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

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include <GLES3/gl31.h>

#include <time.h>

#include "common.h"
#include "shader.h"

#define WINDOW_WIDTH		500
#define WINDOW_HEIGHT		500

#define TEX_WIDTH               256
#define TEX_HEIGHT              256

struct gl_info {
	GLuint render_program;
	GLuint compute_program;
	GLuint texture;
	struct {
		GLuint vertex;
		GLuint color;
	} ssbo;
};

enum {
	GL_SH_LOC_POSITION = 0,
	GL_SH_LOC_COLOR,
	GL_SH_LOC_SRC_TEX,
};
enum {
	GL_SH_BINDING_VERTEX = 3,
	GL_SH_BINDING_COLOR,
};

#define N_BALL		8

#define TO_STRING(x)	#x
static const char *vshader_code = "#version 310 es\n" TO_STRING(
	layout (location=0) in vec4 position;
	layout (location=1) in vec4 color;
	out vec4 vColor;
	void main()
	{
		gl_Position = position;
		gl_PointSize = 20.0;
		vColor = color;
	});

static const char *fshader_code = "#version 310 es\n" TO_STRING(
	out mediump vec4 fragColor;
	in mediump vec4 vColor;
	layout (location=1) uniform mediump sampler2D srcTex;
	void main() {
		fragColor = texture(srcTex, gl_PointCoord) * vColor;
	});

static const char *cshader_code = "#version 310 es\n" TO_STRING(
	layout(std140, binding=3) buffer vertex {
		vec4 p[];
	};
	layout(std140, binding=4) buffer color {
		vec4 c[];
	};
	layout(local_size_x = 4) in;
	void main() {
		uint i = gl_GlobalInvocationID.x;
		float x = -0.8 + float(i) * 0.2;
		p[i] = vec4(x, x, 0.0, 1.0);
		c[i] = vec4(1.0 - float(i) * 0.1, 1.0 - float(i) * 0.1, 0.0,
			    1.0);
	});

#define R       128
static GLuint
create_texture()
{
	GLuint texture;
	uint32_t data[R * 2 * R * 2] = {0};
	uint32_t *p = data;
	int x, y;

	for (y = 0; y < R * 2; y++) {
		for (x = 0; x < R * 2; x++) {
			if ((x - R) * (x - R) + (y - R) * (y - R) < R * R)
				*p = 0xffffffff;
			p++;
		}
	}

	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, R * 2, R * 2, 0, GL_RGBA,
		     GL_UNSIGNED_BYTE, data);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);

	return texture;
}

static void
init_gl(void *data)
{
	struct gl_info *gl = data;
	struct shader_info shader;

	memset(&shader, 0x0, sizeof(shader));
	shader.vertex = vshader_code;
	shader.fragment = fshader_code;
	gl->render_program = shader_build_program(&shader);
	assert(gl->render_program);

	memset(&shader, 0x0, sizeof(shader));
	shader.compute = cshader_code;
	gl->compute_program = shader_build_program(&shader);
	assert(gl->compute_program);

	glUseProgram(gl->compute_program);

	glGenBuffers(1, &gl->ssbo.vertex);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl->ssbo.vertex);
	glBufferData(GL_SHADER_STORAGE_BUFFER, N_BALL * 4 * sizeof(GLfloat), 0,
		     GL_STATIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, GL_SH_BINDING_VERTEX,
			 gl->ssbo.vertex);

	glGenBuffers(1, &gl->ssbo.color);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl->ssbo.color);
	glBufferData(GL_SHADER_STORAGE_BUFFER, N_BALL * 4 * sizeof(GLfloat), 0,
		     GL_STATIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, GL_SH_BINDING_COLOR,
			 gl->ssbo.color);

	glUseProgram(gl->render_program);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	gl->texture = create_texture();
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, gl->texture);
	glUniform1i(GL_SH_LOC_SRC_TEX, 0);

	glBindBuffer(GL_ARRAY_BUFFER, gl->ssbo.vertex);
	glVertexAttribPointer(GL_SH_LOC_POSITION, 4, GL_FLOAT, GL_FALSE, 0, 0);
	glEnableVertexAttribArray(GL_SH_LOC_POSITION);
	glBindBuffer(GL_ARRAY_BUFFER, gl->ssbo.color);
	glVertexAttribPointer(GL_SH_LOC_COLOR, 4, GL_FLOAT, GL_FALSE, 0, 0);
	glEnableVertexAttribArray(GL_SH_LOC_COLOR);
}

static void
deinit_gl(void *data)
{
	struct gl_info *gl = data;

	glDeleteBuffers(1, &gl->ssbo.vertex);
	glDeleteBuffers(1, &gl->ssbo.color);
	glDeleteProgram(gl->render_program);
	glDeleteProgram(gl->compute_program);
	glDeleteTextures(1, &gl->texture);
}

static void
redraw(void *data, struct rect *damage)
{
	struct gl_info *gl = data;

	glUseProgram(gl->compute_program);
	glDispatchCompute(N_BALL / 4, 1, 1);

	glUseProgram(gl->render_program);
	glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
	glClearColor(0.0f, 0.0f, 0.0f, 0.5f);
	glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_POINTS, 0, N_BALL);
}

int
main(int argc, char **argv)
{
	struct gl_info gl;
	struct app_info app = {
		.name = "gl-compute1",
		.id = "jp.co.igel.gl-compute1",
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
