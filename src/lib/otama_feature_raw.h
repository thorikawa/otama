/*
 * This file is part of otama.
 *
 * Copyright (C) 2012 nagadomi@nurs.or.jp
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License,
 * or any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef OTAMA_FEATURE_RAW_H
#define OTAMA_FEATURE_RAW_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*otama_feature_raw_free_t)(void *p);
typedef struct {
	otama_feature_raw_free_t self_free;
	void *raw;
} otama_feature_raw_t;

#ifdef __cplusplus
}
#endif

#endif
