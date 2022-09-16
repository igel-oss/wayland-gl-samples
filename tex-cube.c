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
#include <cairo.h>

#include <GLES2/gl2.h>

#include "shader.h"
#include "common.h"

#define WINDOW_WIDTH		500
#define WINDOW_HEIGHT		500
#define TEX_WIDTH               256
#define TEX_HEIGHT              256

struct gl_info {
	GLuint sh_position;
	GLuint sh_texcoord;
	GLuint sh_texture;
	GLuint sh_proj;
	GLuint sh_model;
	GLuint texture;
	GLuint buffers[3];
	GLuint program;
};

#define TO_STRING(x)	#x
static const char *vert_shader_text = TO_STRING(
	uniform mat4 proj;
	uniform mat4 model;
	attribute vec3 position;
	attribute vec2 texcoord;
	varying vec2 texcoordVarying;
	void main()
	{
		gl_Position = proj * model * vec4(position, 1.0);
		texcoordVarying = texcoord;
	});

static const char *frag_shader_text = TO_STRING(
	precision mediump float;
	varying vec2 texcoordVarying;
	uniform sampler2D texture;
	void main() {
		gl_FragColor = texture2D(texture, texcoordVarying).bgra;
	});

static GLuint
create_texture()
{
	cairo_surface_t *bg;
	cairo_surface_t *img;
	cairo_t *cr;
	unsigned char *data;
	GLuint texture;

	bg = cairo_image_surface_create_from_png("images/back.png");

	img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
					 TEX_WIDTH, TEX_HEIGHT);
	cr = cairo_create(img);

	cairo_set_source_surface(cr, bg, 0, 0);
	cairo_paint(cr);
	cairo_surface_flush(img);
	cairo_destroy(cr);

	data = cairo_image_surface_get_data(img);
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX_WIDTH, TEX_HEIGHT, 0,
		     GL_RGBA, GL_UNSIGNED_BYTE, data);

	cairo_surface_destroy(bg);
	cairo_surface_destroy(img);

	return texture;
}

static void init_gl_buffer(struct gl_info *gl)
{
	static const GLfloat vertex[] = {
		// Front face
		-0.5, -0.5,  0.5,
		0.5, -0.5,  0.5,
		0.5,  0.5,  0.5,
		-0.5,  0.5,  0.5,
		// Back face
		-0.5, -0.5, -0.5,
		-0.5,  0.5, -0.5,
		0.5,  0.5, -0.5,
		0.5, -0.5, -0.5,
		// Top face
		-0.5,  0.5, -0.5,
		-0.5,  0.5,  0.5,
		0.5,  0.5,  0.5,
		0.5,  0.5, -0.5,
		// Bottom face
		-0.5, -0.5, -0.5,
		0.5, -0.5, -0.5,
		0.5, -0.5,  0.5,
		-0.5, -0.5,  0.5,
		// Right face
		0.5, -0.5, -0.5,
		0.5,  0.5, -0.5,
		0.5,  0.5,  0.5,
		0.5, -0.5,  0.5,
		// Left face
		-0.5, -0.5, -0.5,
		-0.5, -0.5,  0.5,
		-0.5,  0.5,  0.5,
		-0.5,  0.5, -0.5
	};
	GLfloat texcoord[] = {
		//front
		0, 0,
		1, 0,
		1, 1,
		0, 1,
		//back
		0, 0,
		1, 0,
		1, 1,
		0, 1,
		//top
		0, 0,
		1, 0,
		1, 1,
		0, 1,
		//bottom
		0, 0,
		1, 0,
		1, 1,
		0, 1,
		//right
		0, 0,
		1, 0,
		1, 1,
		0, 1,
		//left
		0, 0,
		1, 0,
		1, 1,
		0, 1,
	};

	static const GLushort index[] = {
		0,  1,  2,      0,  2,  3,    // Front
		4,  5,  6,      4,  6,  7,    // Back
		8,  9,  10,     8,  10, 11,   // Top
		12, 13, 14,     12, 14, 15,   // Bottom
		16, 17, 18,     16, 18, 19,   // Right
		20, 21, 22,     20, 22, 23    // Left
	};

	glGenBuffers(3, gl->buffers);

	glBindBuffer(GL_ARRAY_BUFFER, gl->buffers[0]);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertex), vertex, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, gl->buffers[1]);
	glBufferData(GL_ARRAY_BUFFER, sizeof(texcoord), texcoord,
		     GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gl->buffers[2]);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(index), index,
		     GL_STATIC_DRAW);
}

static void
init_gl(void *data)
{
	struct gl_info *gl = data;
	struct shader_info shader;

	gl->texture = create_texture();

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
	gl->sh_texcoord = glGetAttribLocation(gl->program, "texcoord");
	gl->sh_texture = glGetUniformLocation(gl->program, "texture");
	gl->sh_proj = glGetUniformLocation(gl->program, "proj");
	gl->sh_model = glGetUniformLocation(gl->program, "model");
	glEnableVertexAttribArray(gl->sh_position);
	glEnableVertexAttribArray(gl->sh_texcoord);

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);

	init_gl_buffer(gl);
}

static void
deinit_gl(void *data)
{
	struct gl_info *gl = data;

	glDeleteBuffers(3, gl->buffers);
	glDeleteTextures(1, &gl->texture);
	glDeleteProgram(gl->program);
}

static void
make_frustum(GLfloat m[4][4], GLfloat left, GLfloat right, GLfloat bottom,
                  GLfloat top, GLfloat znear, GLfloat zfar)
{
	GLfloat X = 2 * znear / (right -left);
	GLfloat Y = 2 * znear / (top - bottom);
	GLfloat A = (right + left) / (right - left);
	GLfloat B = (top + bottom) / (top - bottom);
	GLfloat C = -(zfar + znear) / (zfar - znear);
	GLfloat D = -2 * zfar * znear / (zfar - znear);

	m[0][0] = X; m[1][0] = 0; m[2][0] = A;  m[3][0] = 0;
	m[0][1] = 0; m[1][1] = Y; m[2][1] = B;  m[3][1] = 0;
	m[0][2] = 0; m[1][2] = 0; m[2][2] = C;  m[3][2] = D;
	m[0][3] = 0; m[1][3] = 0; m[2][3] = -1; m[3][3] = 0;
}

void make_projection_matrix(GLfloat m[4][4], GLfloat fovy, GLfloat aspect,
                            GLfloat znear, GLfloat zfar)
{
	GLfloat ymax = znear * tan(fovy * M_PI / 360.0);
	GLfloat ymin = -ymax;
	GLfloat xmin = ymin * aspect;
	GLfloat xmax = ymax * aspect;

	make_frustum(m, xmin, xmax, ymin, ymax, znear, zfar);
}

void mult_matrix(GLfloat a[4][4], GLfloat b[4][4], GLfloat ret[4][4])
{
	int i, j, k;

	memset(ret, 0x00, 4 * 4 * sizeof(GLfloat));
	for (i = 0; i < 4; i++) {
		for (j = 0; j < 4; j++) {
			for (k = 0; k < 4; k++)
				ret[j][i] += a[k][i] * b[j][k];
		}
	}
}

void translation(GLfloat m[4][4], GLfloat x, GLfloat y, GLfloat z)
{
	GLfloat trans[4][4] = {
		{1, 0, 0, 0},
		{0, 1, 0, 0},
		{0, 0, 1, 0},
		{x, y, z, 1},
	};
	GLfloat tmp[4][4];

	memcpy(tmp, m, 4 * 4 * sizeof(GLfloat));
	mult_matrix(tmp, trans, m);
}

void rotation(GLfloat m[4][4], GLfloat x, GLfloat y, GLfloat z)
{
	GLfloat rot_x[4][4] = {
		{1, 0, 0, 0},
		{0, 1, 0, 0},
		{0, 0, 1, 0},
		{0, 0, 0, 1},
	};
	GLfloat rot_y[4][4] = {
		{1, 0, 0, 0},
		{0, 1, 0, 0},
		{0, 0, 1, 0},
		{0, 0, 0, 1},
	};
	GLfloat rot_z[4][4] = {
		{1, 0, 0, 0},
		{0, 1, 0, 0},
		{0, 0, 1, 0},
		{0, 0, 0, 1},
	};
	GLfloat tmp1[4][4], tmp2[4][4];

        rot_x[1][1] =  cos(x);
	rot_x[2][1] =  sin(x);
	rot_x[1][2] = -sin(x);
	rot_x[2][2] =  cos(x);

	rot_y[0][0] = cos(y);
	rot_y[2][0] = sin(y);
	rot_y[0][2] = -sin(y);
	rot_y[2][2] = cos(y);

	rot_z[0][0] = cos(z);
	rot_z[1][0] = sin(z);
	rot_z[0][1] = -sin(z);
	rot_z[1][1] = cos(z);

	mult_matrix(m, rot_x, tmp1);
	mult_matrix(tmp1, rot_y, tmp2);
	mult_matrix(tmp2, rot_z, m);
}

static void
redraw(void *data, struct rect *damage)
{
	struct gl_info *gl = data;

	GLfloat projection[4][4] = {
		{ 1, 0, 0, 0 },
		{ 0, 1, 0, 0 },
		{ 0, 0, 1, 0 },
		{ 0, 0, 0, 1 }
	};
	GLfloat model[4][4] = {
		{ 1, 0, 0, 0 },
		{ 0, 1, 0, 0 },
		{ 0, 0, 1, 0 },
		{ 0, 0, 0, 1 }
	};
	static GLfloat rot_x = 0;
	static GLfloat rot_y = 0;

	rot_x += 0.5;
	rot_y += 0.3;
	if (rot_x > 360) rot_x -= 360;
	if (rot_y > 360) rot_y -= 360;

	make_projection_matrix(projection, 45.0, 1.0, 0.1, 100.0);
	glUniformMatrix4fv(gl->sh_proj, 1, GL_FALSE,
			   (GLfloat *)projection);

	translation(model, 0.0, 0.0, -5.0);
	rotation(model, rot_x * M_PI / 180.0, rot_y * M_PI / 180.0, 0);
	glUniformMatrix4fv(gl->sh_model, 1, GL_FALSE, (GLfloat*)model);

	glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
	glClearColor(0.0, 0.0, 0.0, 0.5);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, gl->texture);
	glUniform1i(gl->sh_texture, 0);

	glBindBuffer(GL_ARRAY_BUFFER, gl->buffers[0]);
	glVertexAttribPointer(gl->sh_position, 3, GL_FLOAT, GL_FALSE, 0, 0);

	glBindBuffer(GL_ARRAY_BUFFER, gl->buffers[1]);
	glVertexAttribPointer(gl->sh_texcoord, 2, GL_FLOAT, GL_FALSE, 0, 0);

	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gl->buffers[2]);
	glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, 0);

	/* return damage region */
	damage->x = WINDOW_WIDTH / 4;
	damage->y = WINDOW_HEIGHT / 4;
	damage->width = WINDOW_WIDTH / 2;
	damage->height = WINDOW_HEIGHT / 2;
}

int
main(int argc, char **argv)
{
	struct gl_info gl;
	struct app_info app = {
		.name = "gl-tex-cube",
		.id = "jp.co.igel.gl-tex-cube",
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
