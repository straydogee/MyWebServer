#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"

using namespace std;

connection_pool::connection_pool()
{
	m_CurConn = 0;
	m_FreeConn = 0;
}

connection_pool *connection_pool::GetInstance()
{
	static connection_pool connPool;
	return &connPool;
}

void connection_pool::check_database(const string &dbname) {
    MYSQL *con = mysql_init(NULL);    
    if (mysql_real_connect(con, m_url.c_str(), m_User.c_str(), m_PassWord.c_str(), NULL, m_Port, NULL, 0) == NULL) {
        LOG_ERROR("MySQL connection error: %s", mysql_error(con));
        exit(1);
    }   

    // 创建数据库，如果不存在
    std::string create_db_query = "CREATE DATABASE IF NOT EXISTS " + dbname;
    if (mysql_query(con, create_db_query.c_str())) {
        LOG_ERROR("Failed to create database: %s", mysql_error(con));
        exit(1);
    }

    // 使用数据库
    std::string use_db_query = "USE " + dbname;
    if (mysql_query(con, use_db_query.c_str())) {
        LOG_ERROR("Failed to use database: %s", mysql_error(con));
        exit(1);
    }

    // 检查表是否存在
    std::string query = "SELECT COUNT(*) FROM information_schema.tables "
                        "WHERE table_schema = '" + dbname + "' AND table_name = 'user';";
    if (mysql_query(con, query.c_str())) {
        LOG_ERROR("Failed to execute query: %s", mysql_error(con));
        exit(1);
    }

    // 获取查询结果
    MYSQL_RES *res = mysql_store_result(con);
    if (res == NULL) {
        LOG_ERROR("mysql_store_result() failed: %s", mysql_error(con));
        exit(1);
    }

    // 读取查询结果
    MYSQL_ROW row = mysql_fetch_row(res);
    int count = (row != NULL) ? atoi(row[0]) : 0;

    // 创建表，如果不存在
    if (count == 0) { // 表不存在
        const char *createTableSQL = "CREATE TABLE user ("
                                      "username CHAR(50) NULL, "
                                      "passwd CHAR(50) NULL"
                                      ") ENGINE=InnoDB;";
        if (mysql_query(con, createTableSQL)) {
            LOG_ERROR("Failed to create table: %s", mysql_error(con));
            exit(1);
        } else {
			LOG_INFO("Table 'user' created successfully.");
        }
    } else {
		LOG_INFO("Table 'user' already exists.");
    }

    // 清理结果集
    mysql_free_result(res);
    // 关闭数据库连接
    mysql_close(con);	
}

//构造初始化
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, int MaxConn, int close_log)
{
	m_url = url;
	m_Port = Port;
	m_User = User;
	m_PassWord = PassWord;
	m_DatabaseName = DBName;
	m_close_log = close_log;
	// 检查并创建数据库
	check_database(m_DatabaseName);

	for (int i = 0; i < MaxConn; i++)
	{
		MYSQL *con = NULL;
		con = mysql_init(con);

		if (con == NULL)
		{
			LOG_ERROR("MySQL init error: %s", mysql_error(con));
			exit(1);
		}
		con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);

		connList.push_back(con);
		++m_FreeConn;
	}

	reserve = sem(m_FreeConn);

	m_MaxConn = m_FreeConn;
}


//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection()
{
	MYSQL *con = NULL;

	if (0 == connList.size())
		return NULL;

	reserve.wait();
	
	lock.lock();

	con = connList.front();
	connList.pop_front();

	--m_FreeConn;
	++m_CurConn;

	lock.unlock();
	return con;
}

//释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL *con)
{
	if (NULL == con)
		return false;

	lock.lock();

	connList.push_back(con);
	++m_FreeConn;
	--m_CurConn;

	lock.unlock();

	reserve.post();
	return true;
}

//销毁数据库连接池
void connection_pool::DestroyPool()
{

	lock.lock();
	if (connList.size() > 0)
	{
		list<MYSQL *>::iterator it;
		for (it = connList.begin(); it != connList.end(); ++it)
		{
			MYSQL *con = *it;
			mysql_close(con);
		}
		m_CurConn = 0;
		m_FreeConn = 0;
		connList.clear();
	}

	lock.unlock();
}

//当前空闲的连接数
int connection_pool::GetFreeConn()
{
	return this->m_FreeConn;
}

connection_pool::~connection_pool()
{
	DestroyPool();
}

connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool){
	*SQL = connPool->GetConnection();
	
	conRAII = *SQL;
	poolRAII = connPool;
}

connectionRAII::~connectionRAII(){
	poolRAII->ReleaseConnection(conRAII);
}