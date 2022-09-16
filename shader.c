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

#include <GLES3/gl31.h>

#include "shader.h"

#define LOG_LENGTH      1024

static GLuint
compile_shader(const char *src, GLenum type)
{
	GLuint shader;
	GLint status;

	if (!src)
		return 0;

	shader = glCreateShader(type);
	if (!shader) {
		fprintf(stderr, "Can't create shader\n");
		return 0;
	}

	glShaderSource(shader, 1, (const char **)&src, NULL);
	glCompileShader(shader);

	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (!status) {
		char log[LOG_LENGTH];
		GLsizei len;
		char *type_str;
		switch (type) {
		case GL_VERTEX_SHADER:
			type_str = "vertex";
			break;
		case GL_FRAGMENT_SHADER:
			type_str = "fragment";
			break;
		default:
			type_str = "";
		}
		glGetShaderInfoLog(shader, LOG_LENGTH, &len, log);
		fprintf(stderr, "Error: compiling %s: %*s\n", type_str, len,
			log);
		return 0;
	}

	return shader;
}

unsigned int
shader_build_program(struct shader_info *shader)
{
	GLuint vshader = 0, fshader = 0, program = 0;
	GLint status;

	vshader = compile_shader(shader->vertex, GL_VERTEX_SHADER);
	fshader = compile_shader(shader->fragment, GL_FRAGMENT_SHADER);
	if (!fshader && !fshader)
		goto exit;

	program = glCreateProgram();
	if (vshader)
		glAttachShader(program, vshader);
	if (fshader)
		glAttachShader(program, fshader);
	glLinkProgram(program);

	glGetProgramiv(program, GL_LINK_STATUS, &status);
	if (!status) {
		char log[LOG_LENGTH];
		GLsizei len;
		glGetProgramInfoLog(program, LOG_LENGTH, &len ,log);
		fprintf(stderr, "Error: linking:\n%*s\n", len, log);
		glDeleteProgram(program);
		program = 0;
	}

exit:
	if (vshader)
		glDeleteShader(vshader);
	if (fshader)
		glDeleteShader(fshader);

	return program;
}
