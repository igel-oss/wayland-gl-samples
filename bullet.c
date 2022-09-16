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
#include <time.h>
#include <cairo.h>

#include <GLES2/gl2.h>

#include "common.h"
#include "shader.h"

#define WINDOW_WIDTH		1280
#define WINDOW_HEIGHT		960

#define TEX_WIDTH               512
#define TEX_HEIGHT              512
#define TEX_BACK_X              0
#define TEX_BACK_Y              0
#define TEX_BULLETS_X           256
#define TEX_BULLETS_Y           0
#define TEX_ASCII_X             0
#define TEX_ASCII_Y             256
#define TEX_PLAYER_X            TEX_BULLETS_X + 16
#define TEX_PLAYER_Y            TEX_BULLETS_Y + 40
#define TEX_ENEMY_X             TEX_BULLETS_X
#define TEX_ENEMY_Y             TEX_BULLETS_Y + 72
#define TEX_BACK_WIDTH          255
#define TEX_BACK_HEIGHT         255
#define TEX_PLAYER_WIDTH        31
#define TEX_PLAYER_HEIGHT       31
#define TEX_ENEMY_WIDTH         63
#define TEX_ENEMY_HEIGHT        63

#define MAX_BULLETS     12000
#define MAX_BUFFER      (MAX_BULLETS + 20) * 18	/* dim=3 x points=6 */

struct player_t {
	int x, y;
	int cw, ch;
	int csx, csy, cex, cey;
};

struct enemy_bullet_t {
	int type;
	int flag;
	int count;
	int color;
	int state;
	int till;
	double x, y, vx, vy;
	double angle, speed, base_ang, rem_sp;
};

struct enemy_t {
	int x, y;
	int cw, ch;
	int csx, csy, cex, cey;
	struct enemy_bullet_t *bullets;
};

struct gl_info {
	GLuint sh_position;
	GLuint sh_texcoord;
	GLuint sh_texture;
	GLuint sh_screen_size;
	GLuint sh_tex_size;
	GLuint texture;
	GLuint program;
};

struct gl_buffer {
	short *vertex_buffer;
	short *texcoord_buffer;
	int p_vertex_buffer;
	int p_texcoord_buffer;
	int vertex_count;
};

struct app {
	struct player_t player;
	struct enemy_t enemy;
	struct gl_info gl;
	struct gl_buffer buf;
};

#define TO_STRING(x)	#x
static const char *vert_shader_text = TO_STRING(
	attribute vec4 position;
	attribute vec2 texcoord;
	varying vec2 texcoordVarying;
	uniform vec2 screenSize;
	uniform vec2 texSize;
	void main()
	{
		vec4 pos = position;
		pos.x = pos.x / screenSize.x - (screenSize.x - pos.x) /
			screenSize.x;
		pos.y = pos.y / screenSize.y - (screenSize.y - pos.y) /
			screenSize.y;
		gl_Position = pos;
		vec2 tex = texcoord;
		tex.x = tex.x / texSize.x;
		tex.y = tex.y / texSize.y;
		texcoordVarying = tex;
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
	cairo_surface_t *bg, *bullet, *ascii;
	cairo_surface_t *img;
	cairo_t *cr;
	unsigned char *data;
	GLuint texture;

	bg = cairo_image_surface_create_from_png("images/back.png");
	bullet = cairo_image_surface_create_from_png("images/img.png");
	ascii = cairo_image_surface_create_from_png("images/ascii_num.png");

	img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
					 TEX_WIDTH, TEX_HEIGHT);
	cr = cairo_create(img);

	cairo_set_source_surface(cr, bg, TEX_BACK_X, TEX_BACK_Y);
	cairo_paint(cr);
	cairo_set_source_surface(cr, bullet, TEX_BULLETS_X, TEX_BULLETS_Y);
	cairo_paint(cr);
	cairo_set_source_surface(cr, ascii, TEX_ASCII_X, TEX_ASCII_Y);
	cairo_paint(cr);
	cairo_surface_flush(img);
	cairo_destroy(cr);

	data = cairo_image_surface_get_data(img);
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX_WIDTH, TEX_HEIGHT, 0,
		     GL_RGBA, GL_UNSIGNED_BYTE, data);

	cairo_surface_destroy(bg);
	cairo_surface_destroy(bullet);
	cairo_surface_destroy(ascii);
	cairo_surface_destroy(img);

	return texture;
}

static void
init_gl_buffer(struct gl_buffer *buf)
{
	buf->vertex_buffer = malloc(MAX_BUFFER * sizeof(short));
	assert(buf->vertex_buffer);
	buf->texcoord_buffer = malloc(MAX_BUFFER * sizeof(short));
	assert(buf->texcoord_buffer);
}

static void
init_gl(void *data)
{
	struct app *app = data;
	struct gl_info *gl = &app->gl;
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
	gl->sh_screen_size = glGetUniformLocation(gl->program, "screenSize");
        gl->sh_tex_size = glGetUniformLocation(gl->program, "texSize");

	glEnableVertexAttribArray(gl->sh_position);
	glEnableVertexAttribArray(gl->sh_texcoord);

	init_gl_buffer(&app->buf);
}

static void
deinit_gl_buffer(struct gl_buffer *buf)
{
	free(buf->vertex_buffer);
	free(buf->texcoord_buffer);
}

static void
deinit_gl(void *data)
{
	struct app *app = data;
	struct gl_info *gl = &app->gl;

	glDeleteTextures(1, &gl->texture);
	glDeleteProgram(gl->program);

	deinit_gl_buffer(&app->buf);
}

static void
player_init(struct player_t *player, int w, int h)
{
	player->x = w / 2;
	player->y = h * 0.1;
	player->cw = player->ch = 36;
	player->csx = player->x - player->cw / 2;
	player->csy = player->y - player->ch / 2;
	player->cex = player->x + player->cw / 2;
	player->cey = player->y + player->ch / 2;
}

static void
enemy_init(struct enemy_t *enemy, int w, int h)
{
	enemy->x = w / 2;
	enemy->y = h * 8 / 10;
	enemy->cw = enemy->ch = 96;
	enemy->csx = enemy->x - enemy->cw / 2;
	enemy->csy = enemy->y - enemy->ch / 2;
	enemy->cex = enemy->x + enemy->cw / 2;
	enemy->cey = enemy->y + enemy->ch / 2;

	enemy->bullets = calloc(1, MAX_BULLETS * sizeof(struct enemy_bullet_t));
	assert(enemy->bullets);
}

static void
enemy_deinit(struct enemy_t *enemy)
{
	free(enemy->bullets);
}

static void
bullet_move(struct enemy_bullet_t *bullet)
{
	bullet->x += cos(bullet->angle) * bullet->speed;
	bullet->y += sin(bullet->angle) * bullet->speed;
	bullet->count++;
}

static void
bullet_move_with_vec(struct enemy_bullet_t* bullet)
{
	bullet->x += bullet->vx;
	bullet->y += bullet->vy;
}

static int
bullet_is_out(struct enemy_bullet_t* bullet, int w, int h)
{
	if (bullet->x < -40 || w + 40 < bullet->x ||
	    bullet->y < -40 || h + 40 < bullet->y) {
		if (bullet->till < bullet->count)
			return 1;
	}
	return 0;
}

static void
bullet_calc(struct enemy_bullet_t *bullets, int w, int h)
{
	int i;
	for (i = 0; i < MAX_BULLETS; i++) {
		if (!bullets[i].flag)
			continue;
		bullet_move(&bullets[i]);
		if (bullet_is_out(&bullets[i], w, h))
			bullets[i].flag = 0;
	}
}

static void
bullet_init(struct enemy_bullet_t *bullet, int tp, int col, double x,
                 double y, double ang, double sp)
{
	bullet->type = tp;
	bullet->color = col;
	bullet->x = x;
	bullet->y = y;
	bullet->angle = ang;
	bullet->speed = sp;
	bullet->count = 0;
}

static int
enemy_usable_bullet(struct enemy_bullet_t* bullets)
{
	int i;
	for (i = 0; i < MAX_BULLETS; i++) {
		if (bullets[i].flag == 0)
			return i;
	}
	return -1;
}

static void
spel_keroame(struct enemy_t *enemy)
{
	struct enemy_bullet_t* bullets = enemy->bullets;
	static int keroameTim;
	static int spelCount = 0;
	double ang;
	int t, n, i, k;

	t = spelCount % 20;
	if (t == 0)
		keroameTim = 190 + rand() % 30 - 15;
	ang = M_PI * 1.5 + M_PI / 6.0 *
		sin(M_PI * 2 / keroameTim * spelCount);
	for (n = 0; n < 8; n++) {
		int k = enemy_usable_bullet(bullets);
		if (k < 0)
			continue;
		bullet_init(&bullets[k], 1, 4, enemy->x, enemy->y,
			    0, 0);
		bullets[k].flag = 1;
		bullets[k].vx = cos(ang - M_PI / 8.0 * 4.0 +
				    M_PI / 8.0 * n + M_PI / 16.0) * 3.0;
		bullets[k].vy = -sin(ang - M_PI / 8.0 * 4.0 + M_PI /
				     8.0 * n + M_PI / 16.0) * 3.0;
		bullets[k].till = 150;
	}

	if (spelCount > 80) {
		for (i = 0; i < 40; i++) {
			ang = (rand() % 270 - 45) * M_PI / 180.0;
			k = enemy_usable_bullet(bullets);
			if (k < 0)
				continue;
			bullet_init(&bullets[k], 2, 4, enemy->x, enemy->y,
				    0, 0);
			bullets[k].flag = 1;
			bullets[k].state = 1;
			bullets[k].vx = cos(ang) * 1.4 * 1.2;
			bullets[k].vy = -sin(ang) * 1.4;
		}
	}
	for (i = 0; i < MAX_BULLETS; i++) {
		double vy;
		if (!bullets[i].flag)
			continue;
		vy = bullets[i].vy;
		if (bullets[i].state == 0) {
			if (bullets[i].count < 150)
				bullets[i].vy -= 0.04;
			bullet_move_with_vec(&bullets[i]);
		} else if (bullets[i].state == 1) {
			if (bullets[i].count < 160)
				bullets[i].vy -= 0.03;
			bullet_move_with_vec(&bullets[i]);
			bullets[i].angle = -atan2(-vy + 0.03, bullets[i].vx);
		}
	}
	spelCount++;
}

static void
enemy_main(struct enemy_t *enemy, int w, int h)
{
	spel_keroame(enemy);
	bullet_calc(enemy->bullets, w, h);
}

static void
add_vertex(struct gl_buffer *buf, int vx, int vy, int uvx, int uvy)
{
	buf->vertex_buffer[buf->p_vertex_buffer] = vx;
	buf->p_vertex_buffer++;
	buf->vertex_buffer[buf->p_vertex_buffer] = vy;
	buf->p_vertex_buffer++;
	buf->vertex_buffer[buf->p_vertex_buffer] = 0;
	buf->p_vertex_buffer++;
	buf->texcoord_buffer[buf->p_texcoord_buffer] = uvx;
	buf->p_texcoord_buffer++;
	buf->texcoord_buffer[buf->p_texcoord_buffer] = uvy;
	buf->p_texcoord_buffer++;
	buf->vertex_count++;
}

static void
add_rect_vertex(struct gl_buffer *buf, int tlx, int tly, int brx, int bry,
		int tluvx, int tluvy, int bruvx, int bruvy)
{
	add_vertex(buf, tlx, bry, tluvx, tluvy);
	add_vertex(buf, brx, bry, bruvx, tluvy);
	add_vertex(buf, tlx, tly, tluvx, bruvy);
	add_vertex(buf, brx, bry, bruvx, tluvy);
	add_vertex(buf, tlx, tly, tluvx, bruvy);
	add_vertex(buf, brx, tly, bruvx, bruvy);
}

static void
redraw(void *data, struct rect *damage)
{
	struct app *app = data;
	struct gl_info *gl = &app->gl;
	int vsx, vsy, vex, vey, usx, usy, uex, uey;
	struct enemy_bullet_t *bullets = app->enemy.bullets;
	int i;
	int n_bullets = 0;

	enemy_main(&app->enemy, WINDOW_WIDTH, WINDOW_HEIGHT);
	app->buf.vertex_count = app->buf.p_vertex_buffer
		= app->buf.p_texcoord_buffer = 0;

	add_rect_vertex(&app->buf, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT,
			TEX_BACK_X, TEX_BACK_Y, TEX_BACK_WIDTH,
			TEX_BACK_HEIGHT);
	add_rect_vertex(&app->buf, app->player.csx, app->player.csy,
			app->player.cex, app->player.cey,
			TEX_PLAYER_X, TEX_PLAYER_Y,
			TEX_PLAYER_X + TEX_PLAYER_WIDTH,
			TEX_PLAYER_Y + TEX_PLAYER_HEIGHT);
	add_rect_vertex(&app->buf, app->enemy.csx, app->enemy.csy,
			app->enemy.cex, app->enemy.cey,
			TEX_ENEMY_X, TEX_ENEMY_Y, TEX_ENEMY_X + TEX_ENEMY_WIDTH,
			TEX_ENEMY_Y + TEX_ENEMY_HEIGHT);

	for (i = 0; i < MAX_BULLETS; i++) {
		if (!bullets[i].flag)
			continue;
		vsx = vsy = vex = vey = usx = usy = uex = uey = 0;
		switch (bullets[i].type) {
		case 0:
			vsx = (int) (bullets[i].x - 4);
			vsy = (int) (bullets[i].y - 4);
			vex = (int) (bullets[i].x + 4);
			vey = (int) (bullets[i].y + 4);
			usx = TEX_BULLETS_X + bullets[i].color * 8;
			usy = TEX_BULLETS_Y;
			uex = usx + 8;
			uey = 8;
			break;
		case 1:
			vsx = (int) (bullets[i].x - 8);
			vsy = (int) (bullets[i].y - 8);
			vex = (int) (bullets[i].x + 8);
			vey = (int) (bullets[i].y + 8);
			usx = TEX_BULLETS_X + bullets[i].color * 16;
			usy = TEX_BULLETS_Y + 8;
			uex = usx + 16;
			uey = usy + 16;
			break;
		case 2:
			vsx = (int) (bullets[i].x - 4);
			vsy = (int) (bullets[i].y - 8);
			vex = (int) (bullets[i].x + 4);
			vey = (int) (bullets[i].y + 8);
			usx = TEX_BULLETS_X + bullets[i].color * 8;
			usy = TEX_BULLETS_Y + 8 + 16;
			uex = usx + 8;
			uey = usy + 16;
			break;
		}
		add_rect_vertex(&app->buf, vsx, vsy, vex, vey, usx, usy, uex,
				uey);
		n_bullets++;
	}

	{
		int n;
		int keta = 0;
		while (n_bullets > 0) {
			n = n_bullets % 10;
			n_bullets /= 10;
			add_rect_vertex(&app->buf,
					WINDOW_WIDTH - 8 * keta - 8,
					WINDOW_HEIGHT - 32,
					WINDOW_WIDTH - 8 * keta,
					WINDOW_HEIGHT - 16,
					TEX_ASCII_X + 8 + n * 8,
					TEX_ASCII_Y,
					TEX_ASCII_X + 8 + n * 8 + 8,
					TEX_ASCII_Y + 16);
			keta++;
		}
	}

	//FPS
	{
		static int fps_count = 0;
		static long long fps_time = 0;
		static float fps = 0;
		struct timespec spec;
		int keta = 0;
		int disp_fps = (int)fps;
		int n;
		long long now;

		clock_gettime( CLOCK_MONOTONIC, &spec );
		now = spec.tv_sec * 1000LL + spec.tv_nsec / 1000000LL;
		fps_count++;
		if (!fps_time) {
			fps_time = now;
		} else {
			long long diff = now - fps_time;
			if (diff >= 1000) {
				fps = fps_count * 1000 / (float) diff;
				fps_time = now;
				fps_count = 0;
			}
		}

		while (disp_fps > 0) {
			n = disp_fps % 10;
			disp_fps /= 10;
			add_rect_vertex(&app->buf,
					WINDOW_WIDTH - 8 * keta -
					8 - 16,
					WINDOW_HEIGHT - 16,
					WINDOW_WIDTH - 8 * keta -
					16,
					WINDOW_HEIGHT,
					TEX_ASCII_X + 8 + n * 8,
					TEX_ASCII_Y,
					TEX_ASCII_X + 8 + n * 8 + 8,
					TEX_ASCII_Y + 16);
			keta++;
		}
		add_rect_vertex(&app->buf,
				WINDOW_WIDTH - 8 - 8,
				WINDOW_HEIGHT - 16,
				WINDOW_WIDTH - 8,
				WINDOW_HEIGHT,
				TEX_ASCII_X + 8 + 14 * 8,
				TEX_ASCII_Y,
				TEX_ASCII_X + 8 + 14 * 8 + 8,
				TEX_ASCII_Y + 16);
		n = (int)(fps * 10) % 10;
		add_rect_vertex(&app->buf,
				WINDOW_WIDTH - 8,
				WINDOW_HEIGHT - 16,
				WINDOW_WIDTH,
				WINDOW_HEIGHT,
				TEX_ASCII_X + 8 + n * 8,
				TEX_ASCII_Y,
				TEX_ASCII_X + 8 + n * 8 + 8,
				TEX_ASCII_Y + 16);
	}

	glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);

	glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, gl->texture);
	glUniform1i(gl->sh_texture, 0);
	glUniform2f(gl->sh_screen_size, WINDOW_WIDTH, WINDOW_HEIGHT);
	glUniform2f(gl->sh_tex_size, TEX_WIDTH, TEX_HEIGHT);

	glVertexAttribPointer(gl->sh_texcoord, 2, GL_SHORT, GL_FALSE, 0,
			      app->buf.texcoord_buffer);
	glVertexAttribPointer(gl->sh_position, 3, GL_SHORT, GL_FALSE, 0,
			      app->buf.vertex_buffer);
	glDrawArrays(GL_TRIANGLES, 0, app->buf.vertex_count);
}

int
main(int argc, char **argv)
{
	struct app app;
	struct app_info info = {
		.name = "gl-bullet",
		.id = "jp.co.igel.gl-bullet",
		.win_width = WINDOW_WIDTH,
		.win_height = WINDOW_HEIGHT,
		.cb = {
			.init_gl = init_gl,
			.deinit_gl = deinit_gl,
			.redraw = redraw,
			.user_data = &app,
		},
	};

	player_init(&app.player, WINDOW_WIDTH, WINDOW_HEIGHT);
	enemy_init(&app.enemy, WINDOW_WIDTH, WINDOW_HEIGHT);

	app_main(argc, argv, &info);

	enemy_deinit(&app.enemy);

	return 0;
}
