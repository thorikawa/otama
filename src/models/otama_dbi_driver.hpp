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

#include "otama_config.h"
#ifndef OTAMA_DBI_DRIVER_H
#define OTAMA_DBI_DRIVER_H

#include "otama_log.h"
#include "otama_dbi.h"
#include "otama_omp_lock.hpp"
#include "otama_driver.hpp"
#include "otama_variant.h"

namespace otama
{
	template<typename T>
	class DBIDriver: public Driver<T>
	{
	protected:
		otama_dbi_t *m_dbi;
		static const int PULL_LIMIT = 100000;
		
		virtual otama_status_t
		exists_master(bool &exists, uint64_t &seq,
					  const otama_id_t *id)
		{
			char id_hexstr[OTAMA_ID_HEXSTR_LEN];
			otama_dbi_result_t *res;
			otama_status_t ret = OTAMA_STATUS_OK;
			
#ifdef _OPENMP
			OMPLock lock(this->m_lock);
#endif
			otama_id_bin2hexstr(id_hexstr, id);
			res = otama_dbi_queryf(m_dbi,
								   "SELECT id FROM %s WHERE otama_id = '%s';",
								   this->table_name().c_str(),
								   id_hexstr);
			if (!res) {
				ret = OTAMA_STATUS_SYSERROR;
			} else {
				if (otama_dbi_result_next(res)) {
					seq = otama_dbi_result_int64(res, 0);
					exists = true;
				} else {
					exists = false;
				}
				otama_dbi_result_free(&res);
			}
	
			return ret;
		}
		
		virtual otama_status_t
		update_flag(const otama_id_t *id, uint8_t flag)
		{
			otama_dbi_result_t *res;
			char id_hexstr[OTAMA_ID_HEXSTR_LEN];
			
#ifdef _OPENMP
			OMPLock lock(this->m_lock);
#endif

			otama_id_bin2hexstr(id_hexstr, id);

			if (otama_dbi_begin(m_dbi) != 0) {
				return OTAMA_STATUS_SYSERROR;
			}
			res = otama_dbi_queryf(m_dbi,
								   "SELECT flag FROM %s WHERE otama_id = '%s' AND flag != %u;",
								   this->table_name().c_str(),
								   id_hexstr,
								   (unsigned int)flag);
			if (!res) {
				otama_dbi_rollback(m_dbi);
				return OTAMA_STATUS_SYSERROR;
			}
			if (otama_dbi_result_next(res)) {
				int64_t seq = 0;
				int ret;
				
				otama_dbi_result_free(&res);
				ret = otama_dbi_sequence_next(m_dbi, &seq, this->table_name().c_str());
				if (ret != 0) {
					otama_dbi_rollback(m_dbi);
					return OTAMA_STATUS_SYSERROR;
				}
				ret = otama_dbi_execf(
					m_dbi,
					"UPDATE %s SET flag = %u, commit_id = %"PRId64" WHERE otama_id = '%s';",
					this->table_name().c_str(),
					(unsigned int)flag,
					seq,
					id_hexstr);
				if (ret != 0) {
					otama_dbi_rollback(m_dbi);
					return OTAMA_STATUS_SYSERROR;
				}
			} else {
				otama_dbi_result_free(&res);
			}
			if (otama_dbi_commit(m_dbi) != 0) {
				return OTAMA_STATUS_SYSERROR;
			}
			
			return OTAMA_STATUS_OK;
		}
		
		virtual otama_status_t
		insert(const otama_id_t *id,
			   const T *fv)
		{
			otama_status_t ret = OTAMA_STATUS_OK;
			otama_dbi_result_t *res;
			char id_hexstr[OTAMA_ID_HEXSTR_LEN];
			char *fv_str = this->feature_serialize(fv);
			size_t esc_len = strlen(fv_str) * 2 + 3;
			char *fv_esc = nv_alloc_type(char, esc_len);
			
#ifdef _OPENMP
			OMPLock lock(this->m_lock);
#endif
			otama_id_bin2hexstr(id_hexstr, id);
			res = otama_dbi_queryf(
				m_dbi,
				"INSERT INTO %s (otama_id, vector) "
				"  SELECT '%s', %s "
				"    WHERE NOT EXISTS(SELECT otama_id FROM %s WHERE otama_id='%s');",
				this->table_name().c_str(),
				id_hexstr,
				otama_dbi_escape(m_dbi, fv_esc, esc_len, fv_str),
				this->table_name().c_str(),
				id_hexstr
				);
			if (!res) {
				ret = OTAMA_STATUS_SYSERROR;
			} else {
				otama_dbi_result_free(&res);
			}
			nv_free(fv_str);
			nv_free(fv_esc);
			
			return ret;
		}
		
		virtual otama_status_t
		load(const otama_id_t *id,
			 T *fv)
		{
			char id_hexstr[OTAMA_ID_HEXSTR_LEN];
			otama_dbi_result_t *res;
			otama_status_t ret = OTAMA_STATUS_OK;
			
#ifdef _OPENMP
			OMPLock lock(this->m_lock);
#endif
			otama_id_bin2hexstr(id_hexstr, id);
			res = otama_dbi_queryf(m_dbi,
								   "SELECT vector FROM %s WHERE otama_id = '%s';",
								   this->table_name().c_str(),
								   id_hexstr);
			if (!res) {
				ret = OTAMA_STATUS_SYSERROR;
			} else {
				if (otama_dbi_result_next(res)) {
					const char *vec = otama_dbi_result_string(res, 0);
					int ng;
					
					ng = this->feature_deserialize(fv, vec);
					if (ng != 0) {
						OTAMA_LOG_ERROR("invalid vector string. id(%s), size(%zd), vec(%s)",
										id_hexstr, strlen(vec), vec);
						ret = OTAMA_STATUS_ASSERTION_FAILURE;
					}
				} else {
					ret = OTAMA_STATUS_NODATA;
				}
				otama_dbi_result_free(&res);
			}
			
			return ret;
		}
		
		void
		read_dbi_config(otama_dbi_config_t *config,
						otama_variant_t *options)
		{
			const char *driver = otama_variant_hash_at3(options, "driver");
			const char *host = otama_variant_hash_at3(options, "host");
			const char *port = otama_variant_hash_at3(options, "port");
			const char *dbname = otama_variant_hash_at3(options, "name");
			const char *username = otama_variant_hash_at3(options, "user");
			const char *password = otama_variant_hash_at3(options, "password");
			
			memset(config, 0, sizeof(*config));
			
			if (driver) {
				otama_dbi_config_driver(config, driver);
			}
			if (host) {
				otama_dbi_config_host(config, host);
			}
			if (dbname) {
				otama_dbi_config_dbname(config, dbname);
			}
			if (username) {
				otama_dbi_config_username(config, username);
			}
			if (password) {
				otama_dbi_config_password(config, password);
			}
			if (port) {
				otama_dbi_config_port(config, strtol(port, NULL, 10));
			}
			OTAMA_LOG_DEBUG("database[driver]   => %s", driver);
			OTAMA_LOG_DEBUG("database[host]     => %s", host);
			OTAMA_LOG_DEBUG("database[port]     => %s", port);
			OTAMA_LOG_DEBUG("database[name]     => %s", dbname);
			OTAMA_LOG_DEBUG("database[user]     => %s", username);
			OTAMA_LOG_DEBUG("database[password] => * length(%d)",
							password == NULL ? 0 : strlen(password));
		}

		otama_status_t
		select_max_ids(int64_t *max_id, int64_t *max_commit_id)
		{
			otama_dbi_result_t *res = NULL;
			res = otama_dbi_queryf(this->m_dbi,
								   "SELECT MAX(id), MAX(commit_id) FROM %s;",
								   this->table_name().c_str());
			if (res == NULL) {
				return OTAMA_STATUS_SYSERROR;
			}
			otama_dbi_result_next(res);
			*max_id = otama_dbi_result_is_null(res, 0) ? -1 : otama_dbi_result_int64(res, 0);
			*max_commit_id = otama_dbi_result_is_null(res, 1) ? -1 : otama_dbi_result_int64(res, 1);
			
			otama_dbi_result_free(&res);
			
			return OTAMA_STATUS_OK;
		}
		
		otama_dbi_result_t *
		select_new_records(int64_t last_id, int64_t max_id)
		{
			otama_dbi_result_t *res = NULL;
			
			if (last_id == -1) {
				if (this->m_hash_conditions.empty()) {
					res = otama_dbi_queryf(
						this->m_dbi,
						" SELECT id, otama_id, vector FROM %s "
						" WHERE id <= %" PRId64
						" ORDER BY id LIMIT %d;",
						this->table_name().c_str(),
						max_id,
						PULL_LIMIT
						);
				} else {
					res = otama_dbi_queryf(
						this->m_dbi,
						" SELECT id, otama_id, vector FROM %s "
						" WHERE id <= %" PRId64
						" AND %s "
						" ORDER BY id LIMIT %d;",
						this->table_name().c_str(),
						max_id,
						this->m_hash_conditions.c_str(),
						PULL_LIMIT
						);
				}
			} else {
				if (this->m_hash_conditions.empty()) {
					res = otama_dbi_queryf(
						this->m_dbi,
						" SELECT id, otama_id, vector FROM %s "
						" WHERE id > %" PRId64 " AND id <= %" PRId64
						" ORDER BY id LIMIT %d;",
						this->table_name().c_str(),
						last_id, max_id,
						PULL_LIMIT
						);
				} else {
					res = otama_dbi_queryf(
						this->m_dbi,
						" SELECT id, otama_id, vector FROM %s "
						" WHERE id > %" PRId64 " AND id <= %" PRId64
						" AND %s "
						" ORDER BY id LIMIT %d;",
						this->table_name().c_str(),
						last_id, max_id,
						this->m_hash_conditions.c_str(),
						PULL_LIMIT
						);
				}
			}
			return res;
		}
		
		otama_dbi_result_t *
		select_updated_records(int64_t last_commit_id,
							   int64_t max_commit_id)
		{
			otama_dbi_result_t *res = NULL;
			
			if (this->m_hash_conditions.empty()) {
				res = otama_dbi_queryf(
					this->m_dbi,
					" SELECT id, flag, commit_id FROM %s "
					" WHERE commit_id > %"PRId64 " AND commit_id <= %" PRId64
					" ORDER BY commit_id;",
					this->table_name().c_str(),
					last_commit_id, max_commit_id);
			} else {
				res = otama_dbi_queryf(
					this->m_dbi,
					" SELECT id, flag, commit_id FROM %s "
					" WHERE  commit_id > %"PRId64 " AND commit_id <= %" PRId64
					"       AND %s "
					" ORDER BY commit_id;",
					this->table_name().c_str(),
					last_commit_id, max_commit_id,
					this->m_hash_conditions.c_str());
			}
			return res;
		}
		
	public:
		DBIDriver(otama_variant_t *options)
			: Driver<T>(options)
		{
			otama_variant_t *database = otama_variant_hash_at(options, "database");
			otama_dbi_config_t dbi_config;
			
			this->read_dbi_config(&dbi_config, database);
			m_dbi = otama_dbi_new(&dbi_config);
		}
		
		virtual otama_status_t
		open(void)
		{
			otama_status_t ret;
	
#ifdef _OPENMP
			OMPLock lock(this->m_lock);
#endif
			ret = Driver<T>::open();
			if (ret != OTAMA_STATUS_OK) {
				return ret;
			}
			if (otama_writeable_directory(this->data_dir().c_str()) != 0) {
				OTAMA_LOG_ERROR("cannot touch: %s", this->data_dir().c_str());
				return OTAMA_STATUS_SYSERROR;
			}
			
			if (otama_dbi_open(m_dbi) != 0) {
				return OTAMA_STATUS_SYSERROR;
			}
			
			return OTAMA_STATUS_OK;
		}
		
		virtual otama_status_t
		create_database(void)
		{
			static const otama_dbi_column_t column_def[] = {
				{ "id", OTAMA_DBI_COLUMN_INT64_PKEY_AUTO, 0, 0, 0, NULL },
				{ "otama_id", OTAMA_DBI_COLUMN_CHAR, 40, 0, 0, NULL },
				{ "vector", OTAMA_DBI_COLUMN_TEXT, 0, 0, 1, NULL },
				{ "flag", OTAMA_DBI_COLUMN_INT, 0, 0, 0, "0" },
				{ "commit_id", OTAMA_DBI_COLUMN_INT64, 0, 0, 0, NULL }
			};
			static const otama_dbi_column_t index_otama_id[] = {
				{ "otama_id", OTAMA_DBI_COLUMN_CHAR, 40, 0, 0, NULL }
			};
			static const otama_dbi_column_t index_otama_id_and_flag[] = {
				{ "otama_id", OTAMA_DBI_COLUMN_CHAR, 40, 0, 0, NULL },
				{ "flag", OTAMA_DBI_COLUMN_INT, 0, 0, 0, "0" }
			};
			static const otama_dbi_column_t index_commit_id[] = {
				{ "commit_id", OTAMA_DBI_COLUMN_INT64, 0, 0, 0, NULL }
			};
			int ret;
			int exist;
			
#ifdef _OPENMP
			OMPLock lock(this->m_lock);
#endif
			ret = otama_dbi_table_exist(m_dbi, &exist, this->table_name().c_str());
			if (ret) {
				return OTAMA_STATUS_SYSERROR;
			}
			if (exist == 0) {
				ret = otama_dbi_create_table(m_dbi,
											 this->table_name().c_str(),
											 column_def,
											 sizeof(column_def)
											 / sizeof(otama_dbi_column_t));
				if (ret != 0) {
					return OTAMA_STATUS_SYSERROR;
				}
				ret = otama_dbi_create_index(m_dbi,
											 this->table_name().c_str(),
											 OTAMA_DBI_UNIQUE_TRUE,
											 index_otama_id,
											 sizeof(index_otama_id)
											 / sizeof(otama_dbi_column_t));
				if (ret != 0) {
					return OTAMA_STATUS_SYSERROR;
				}
				ret = otama_dbi_create_index(
					m_dbi,
					this->table_name().c_str(),
					OTAMA_DBI_UNIQUE_FALSE,
					index_otama_id_and_flag,
					sizeof(index_otama_id_and_flag)
					/ sizeof(otama_dbi_column_t));
				if (ret != 0) {
					return OTAMA_STATUS_SYSERROR;
				}
				ret = otama_dbi_create_index(
					m_dbi,
					this->table_name().c_str(),
					OTAMA_DBI_UNIQUE_FALSE,
					index_commit_id,
					sizeof(index_commit_id)
					/ sizeof(otama_dbi_column_t));
				if (ret != 0) {
					return OTAMA_STATUS_SYSERROR;
				}
				
				ret = otama_dbi_sequence_exist(m_dbi, &exist, this->table_name().c_str());
				if (ret != 0) {
					return OTAMA_STATUS_SYSERROR;
				}
				if (exist) {
					otama_dbi_drop_sequence(m_dbi, this->table_name().c_str());
				}
				ret = otama_dbi_create_sequence(m_dbi, this->table_name().c_str());
				if (ret != 0) {
					return OTAMA_STATUS_SYSERROR;
				}
			}
			
			return OTAMA_STATUS_OK;
		}
		
		virtual otama_status_t
		drop_database(void)
		{
			int ret = 0;
			int exist;
			otama_status_t status = OTAMA_STATUS_OK;
			
#ifdef _OPENMP
			OMPLock lock(this->m_lock);
#endif
			ret = otama_dbi_table_exist(m_dbi, &exist, this->table_name().c_str());
			if (ret == 0) {
				if (exist != 0) {
					ret = otama_dbi_drop_table(m_dbi, this->table_name().c_str());
				}
			} else {
				status = OTAMA_STATUS_SYSERROR;
			}
			
			ret = otama_dbi_sequence_exist(m_dbi, &exist, this->table_name().c_str());
			if (ret == 0) {
				if (exist != 0) {
					ret = otama_dbi_drop_sequence(m_dbi, this->table_name().c_str());
				}
			} else {
				status = OTAMA_STATUS_SYSERROR;
			}
			
			return status;
		}

		virtual bool
		is_active(void)
		{
			return otama_dbi_active(m_dbi) != 0;
		}
		
		virtual otama_status_t
		remove(const otama_id_t *id)
		{
			return update_flag(id, Driver<T>::FLAG_DELETE);
		}

		virtual otama_status_t
		close(void)
		{
			if (m_dbi) {
				otama_dbi_close(&m_dbi);
			}
			return Driver<T>::close();
		}
		
		virtual ~DBIDriver()
		{
			close();
		}
		
		using Driver<T>::insert;
	};
}

#endif

