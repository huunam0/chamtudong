//
// File:   main.cc
// Author: sempr
// refacted by Tran Huu Nam
/*
 * Copyright 2012 sempr <iamsempr@gmail.com>
 *
 * Refacted and modified by Tran Huu Nam <huunam0@gmail.com>
 * Bug report email huunam0@gmail.com
 *
 *
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>
#include <stdarg.h>
#include <ctype.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/signal.h>
//#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <mysql/mysql.h>

#include "okcalls.h"

#define STD_MB 1048576
#define STD_T_LIM 2
#define STD_F_LIM (STD_MB<<5)
#define STD_M_LIM (STD_MB<<7)
#define BUFFER_SIZE 512

#define OJ_WT0 0
#define OJ_WT1 1
#define OJ_CI 2
#define OJ_RI 3
#define OJ_AC 4
#define OJ_PE 5 //presentation error
#define OJ_WA 6
#define OJ_TL 7
#define OJ_ML 8
#define OJ_OL 9
#define OJ_RE 10 //runtime error
#define OJ_CE 11 //compile error
#define OJ_CO 12

/*copy from ZOJ
 http://code.google.com/p/zoj/source/browse/trunk/judge_client/client/tracer.cc?spec=svn367&r=367#39
 */
#ifdef __i386
#define REG_SYSCALL orig_eax
#define REG_RET eax
#define REG_ARG0 ebx
#define REG_ARG1 ecx
#else
#define REG_SYSCALL orig_rax
#define REG_RET rax
#define REG_ARG0 rdi
#define REG_ARG1 rsi

#endif

static int DEBUG = 0;
static char host_name[BUFFER_SIZE];
static char user_name[BUFFER_SIZE];
static char password[BUFFER_SIZE];
static char db_name[BUFFER_SIZE];
static char oj_home[BUFFER_SIZE];
static int port_number;
static int max_running;
static int sleep_time;
static int java_time_bonus = 5;
static int java_memory_bonus = 512;
static char java_xms[BUFFER_SIZE];
static char java_xmx[BUFFER_SIZE];
static int sim_enable = 0;
static int oi_mode=0;
static int use_max_time=0;

static int http_judge=0;
static char http_baseurl[BUFFER_SIZE];

static char http_username[BUFFER_SIZE];
static char http_password[BUFFER_SIZE];

static int shm_run=0;

//static int sleep_tmp;
//#define ZOJ_COM
MYSQL *conn;

static char lang_ext[10][8] = { "c", "cc", "pas", "java", "rb", "sh", "py", "php","pl", "cs" };
//static char buf[BUFFER_SIZE];

long get_file_size(const char * filename) {
        struct stat f_stat;

        if (stat(filename, &f_stat) == -1) {
                return 0;
        }

        return (long) f_stat.st_size;
}

void write_log(const char *fmt, ...) {
        va_list ap;
        char buffer[4096];
        //      time_t          t = time(NULL);
        //int l;
        sprintf(buffer,"%s/log/client.log",oj_home);
        FILE *fp = fopen(buffer, "a+");
        if (fp == NULL) {
                fprintf(stderr, "openfile error!\n");
                system("pwd");
        }
        va_start(ap, fmt);
        //l =
        vsprintf(buffer, fmt, ap);
        fprintf(fp, "%s\n", buffer);
        if (DEBUG)
                printf("%s\n", buffer);
        va_end(ap);
        fclose(fp);

}
int execute_cmd(const char * fmt, ...) {
        char cmd[BUFFER_SIZE];

        int ret = 0;
        va_list ap;

        va_start(ap, fmt);
        vsprintf(cmd, fmt, ap);
        ret = system(cmd);
        va_end(ap);
        return ret;
}


int call_counter[512];

void init_syscalls_limits(int lang) {
        int i;
        memset(call_counter, 0, sizeof(call_counter));
        if (DEBUG)
                write_log("init_call_counter:%d", lang);
        if (lang <= 1) { // C & C++
                for (i = 0; LANG_CC[i]; i++) {
                        call_counter[LANG_CV[i]] = LANG_CC[i];
                }
        } else if (lang == 2) { // Pascal
                for (i = 0; LANG_PC[i]; i++)
                        call_counter[LANG_PV[i]] = LANG_PC[i];
        } else if (lang == 3) { // Java
                for (i = 0; LANG_JC[i]; i++)
                        call_counter[LANG_JV[i]] = LANG_JC[i];
        } else if (lang == 4) { // Ruby
                for (i = 0; LANG_RC[i]; i++)
                        call_counter[LANG_RV[i]] = LANG_RC[i];
        } else if (lang == 5) { // Bash
                for (i = 0; LANG_BC[i]; i++)
                        call_counter[LANG_BV[i]] = LANG_BC[i];
        }else if (lang == 6) { // Python
                for (i = 0; LANG_YC[i]; i++)
                        call_counter[LANG_YV[i]] = LANG_YC[i];
        }else if (lang == 7) { // php
            for (i = 0; LANG_PHC[i]; i++)
                    call_counter[LANG_PHV[i]] = LANG_PHC[i];
    }else if (lang == 8) { // perl
            for (i = 0; LANG_PLC[i]; i++)
                    call_counter[LANG_PLV[i]] = LANG_PLC[i];
    }else if (lang == 9) { // mono c#
            for (i = 0; LANG_CSC[i]; i++)
                    call_counter[LANG_CSV[i]] = LANG_CSC[i];
    }


}

int after_equal(char * c){
        int i=0;
        for(;c[i]!='\0'&&c[i]!='=';i++);
        return ++i;
}
void trim(char * c){
    char buf[BUFFER_SIZE];
    char * start,*end;
    strcpy(buf,c);
    start=buf;
    while(isspace(*start)) start++;
    end=start;
    while(!isspace(*end)) end++;
    *end='\0';
    strcpy(c,start);
}
bool read_buf(char * buf,const char * key,char * value){
   if (strncmp(buf,key, strlen(key)) == 0) {
                strcpy(value, buf + after_equal(buf));
                trim(value);
                if (DEBUG) printf("%s\n",value);
                return 1;
   }
   return 0;
}
void read_int(char * buf,const char * key,int * value){
        char buf2[BUFFER_SIZE];
        if (read_buf(buf,key,buf2))
                sscanf(buf2, "%d", value);

}
// read the configue file
void init_mysql_conf() {
        FILE *fp=NULL;
        char buf[BUFFER_SIZE];
        host_name[0] = 0;
        user_name[0] = 0;
        password[0] = 0;
        db_name[0] = 0;
        port_number = 3306;
        max_running = 3;
        sleep_time = 3;
        strcpy(java_xms, "-Xms32m");
        strcpy(java_xmx, "-Xmx256m");
        sprintf(buf,"%s/etc/judge.conf",oj_home);
        fp = fopen("./etc/judge.conf", "r");
        if(fp!=NULL){
                while (fgets(buf, BUFFER_SIZE - 1, fp)) {
                        read_buf(buf,"OJ_HOST_NAME",host_name);
                        read_buf(buf, "OJ_USER_NAME",user_name);
                        read_buf(buf, "OJ_PASSWORD",password);
                        read_buf(buf, "OJ_DB_NAME",db_name);
                        read_int(buf , "OJ_PORT_NUMBER", &port_number);
                        read_int(buf, "OJ_JAVA_TIME_BONUS", &java_time_bonus);
                        read_int(buf, "OJ_JAVA_MEMORY_BONUS", &java_memory_bonus);
                        read_int(buf , "OJ_SIM_ENABLE", &sim_enable);
                        read_buf(buf,"OJ_JAVA_XMS",java_xms);
                        read_buf(buf,"OJ_JAVA_XMX",java_xmx);
                        read_int(buf,"OJ_HTTP_JUDGE",&http_judge);
                        read_buf(buf,"OJ_HTTP_BASEURL",http_baseurl);
                        read_buf(buf,"OJ_HTTP_USERNAME",http_username);
                        read_buf(buf,"OJ_HTTP_PASSWORD",http_password);
                        read_int(buf , "OJ_OI_MODE", &oi_mode);
                        read_int(buf , "OJ_SHM_RUN", &shm_run);
                        read_int(buf , "OJ_USE_MAX_TIME", &use_max_time);

                }
                //fclose(fp);
        }
}

int isInFile(const char fname[]) {
        int l = strlen(fname);
        if (l <= 3 || strcmp(fname + l - 3, ".in") != 0)
                return 0;
        else
                return l - 3;
}

void find_next_nonspace(int & c1, int & c2, FILE *& f1, FILE *& f2, int & ret) {
        // Find the next non-space character or \n.
        while ((isspace(c1)) || (isspace(c2))) {
                if (c1 != c2) {
                        if (c2 == EOF) {
                                do {
                                        c1 = fgetc(f1);
                                } while (isspace(c1));
                                continue;
                        } else if (c1 == EOF) {
                                do {
                                        c2 = fgetc(f2);
                                } while (isspace(c2));
                                continue;
                        } else if ((c1 == '\r' && c2 == '\n')) {
                                c1 = fgetc(f1);
                        } else if ((c2 == '\r' && c1 == '\n')) {
                                c2 = fgetc(f2);
                        } else {
                                if (DEBUG)
                                        printf("%d=%c\t%d=%c", c1, c1, c2, c2);
                                ;
                                ret = OJ_PE;
                        }
                }
                if (isspace(c1)) {
                        c1 = fgetc(f1);
                }
                if (isspace(c2)) {
                        c2 = fgetc(f2);
                }
        }
}

/***
 int compare_diff(const char *file1,const char *file2){
 char diff[1024];
 sprintf(diff,"diff -q -B -b -w --strip-trailing-cr %s %s",file1,file2);
 int d=system(diff);
 if (d) return OJ_WA;
 sprintf(diff,"diff -q -B --strip-trailing-cr %s %s",file1,file2);
 int p=system(diff);
 if (p) return OJ_PE;
 else return OJ_AC;

 }
 */
 /* add result of each test */
void addrun_test(int solution_id, int test_id, int error, char* output) {
        char sql[1000];
        sprintf(sql,"INSERT into ctd_run (`solution`,`test`,`status`,`moment`,`output`) value (%d,%d,%d,NOW(),'%s')",solution_id, test_id, error, output);
        if (mysql_real_query(conn, sql, strlen(sql)))
            write_log("Error in insert test output here %d, %d: %d",solution_id, test_id, error);
}
/*
 * translated from ZOJ judger r367
 * http://code.google.com/p/zoj/source/browse/trunk/judge_client/client/text_checker.cc#25
 *
 */

int compare_zoj(const char *file1, const char *file2) {
        int ret = OJ_AC;
        FILE * f1, *f2;
        f1 = fopen(file1, "r");
        f2 = fopen(file2, "r");
        if (!f1 || !f2) {
                ret = OJ_RE;
        } else
                for (;;) {
                        // Find the first non-space character at the beginning of line.
                        // Blank lines are skipped.
                        int c1 = fgetc(f1);
                        int c2 = fgetc(f2);
                        find_next_nonspace(c1, c2, f1, f2, ret);
                        // Compare the current line.
                        for (;;) {
                                // Read until 2 files return a space or 0 together.
                                while ((!isspace(c1) && c1) || (!isspace(c2) && c2)) {
                                        if (c1 == EOF && c2 == EOF) {
                                                goto end;
                                        }
                                        if (c1 == EOF || c2 == EOF) {
                                                break;
                                        }
                                        if (c1 != c2) {
                                                // Consecutive non-space characters should be all exactly the same
                                                ret = OJ_WA;
                                                goto end;
                                        }
                                        c1 = fgetc(f1);
                                        c2 = fgetc(f2);
                                }
                                find_next_nonspace(c1, c2, f1, f2, ret);
                                if (c1 == EOF && c2 == EOF) {
                                        goto end;
                                }
                                if (c1 == EOF || c2 == EOF) {
                                        ret = OJ_WA;
                                        goto end;
                                }

                                if ((c1 == '\n' || !c1) && (c2 == '\n' || !c2)) {
                                        break;
                                }
                        }
                }
        end: if (f1)
                fclose(f1);
        if (f2)
                fclose(f2);
        return ret;
}

void delnextline(char s[]) {
        int L;
        L = strlen(s);
        while (L > 0 && (s[L - 1] == '\n' || s[L - 1] == '\r'))
                s[--L] = 0;
}

int compare(int solution_id, int test_id, const char *file1, const char *file2) {
#ifdef ZOJ_COM
        //compare ported and improved from zoj don't limit file size
        return compare_zoj(file1, file2);
#endif
#ifndef ZOJ_COM
        //the original compare from the first version of hustoj has file size limit
        //and waste memory
        FILE *f1,*f2;
        char *s1,*s2,*p1,*p2;
        int PEflg;
        s1=new char[STD_F_LIM+512];
        s2=new char[STD_F_LIM+512];
        if (!(f1=fopen(file1,"r")))
        return OJ_AC;
        for (p1=s1;EOF!=fscanf(f1,"%s",p1);)
        while (*p1) p1++;
        fclose(f1);
        if (!(f2=fopen(file2,"r")))
        return OJ_RE;
        for (p2=s2;EOF!=fscanf(f2,"%s",p2);)
        while (*p2) p2++;
        fclose(f2);
		//write_log("jusding '%s' vs '%s'",s1,s2);
        /*
        if (strcmp(s1,s2)!=0) {
                //              printf("A:%s\nB:%s\n",s1,s2);
                delete[] s1;
                delete[] s2;

                return OJ_WA;
        } else {
        */
                f1=fopen(file1,"r");
                f2=fopen(file2,"r");
                PEflg=0;
                //char* pch;
                while (PEflg==0 && fgets(s1,STD_F_LIM,f1) && fgets(s2,STD_F_LIM,f2)) {
                    delnextline(s1);
                    delnextline(s2);
                    //BEGIN-THNAM-20120921
                    //if (strcmp(s1,s2)==0) continue;
                    //else PEflg=1;
                    int i = strlen(s2)-1;
                    while ((i>0) && (s2[i]==' '))
                    {
                        s2[i]='\0';
                        i--;
                    }
                    bool trung=(strcmp(s1,s2)==0);
                    char* pch = strtok (s1,"|");
                    while (pch != NULL)
                    {
                        //write_log("test %s",pch);
						if (strcmp(pch,s2)==0)
                        {
                            trung=true;
                            break;
                        }
                        pch = strtok (NULL, "|");
                    }

                    if (trung) continue;
                    else
                    {
                        PEflg=1;
                        write_log("Error '%s' vs '%s'",s1,s2);
                        addrun_test(solution_id,test_id,PEflg,s2);
                    }

                    //END-THNAM-20120921
                }
				//BEGIN-THNAM-20120930
				//if (PEflg) {
					//write_log("Error '%s' vs '%s'",s1,s2);
					//addrun_test(solution_id,test_id,PEflg,s2);
				//}
				//END-THNAM-20120930
                delete [] s1;
                delete [] s2;
                fclose(f1);fclose(f2);
                if (PEflg) return OJ_PE;
                else return OJ_AC;
        //}
#endif
}

FILE * read_cmd_output(const char * fmt, ...) {
        char cmd[BUFFER_SIZE];

        FILE *  ret =NULL;
        va_list ap;

        va_start(ap, fmt);
        vsprintf(cmd, fmt, ap);
        va_end(ap);
        if(DEBUG) printf("%s\n",cmd);
        ret = popen(cmd,"r");

        return ret;
}
bool check_login(){
        const char  * cmd=" wget --post-data=\"checklogin=1\" --load-cookies=cookie --save-cookies=cookie --keep-session-cookies -q -O - \"%s/admin/problem_judge.php\"";
        int ret=0;
        FILE * fjobs=read_cmd_output(cmd,http_baseurl);
        fscanf(fjobs,"%d",&ret);
        pclose(fjobs);

        return ret;
}
void login(){
        if(!check_login()){
                const char * cmd="wget --post-data=\"user_id=%s&password=%s\" --load-cookies=cookie --save-cookies=cookie --keep-session-cookies -q -O - \"%s/login.php\"";
                FILE * fjobs=read_cmd_output(cmd,http_username,http_password,http_baseurl);
                //fscanf(fjobs,"%d",&ret);
                pclose(fjobs);
        }

}
/* write result back to database */
void _update_solution_mysql(int solution_id, int result, int time, int memory,
                int sim, int sim_s_id,double pass_rate) {
        char sql[BUFFER_SIZE];
        if(oi_mode){
                   sprintf(
                        sql,
                        "UPDATE solution SET result=%d,time=%d,memory=%d,judgetime=NOW(),pass_rate=%f WHERE solution_id=%d LIMIT 1%c",
                        result, time, memory, pass_rate,solution_id, 0);
        }else{
                        sprintf(
                        sql,
                        "UPDATE solution SET result=%d,time=%d,memory=%d,judgetime=NOW() WHERE solution_id=%d LIMIT 1%c",
                        result, time, memory, solution_id, 0);
                }
        //      printf("sql= %s\n",sql);
        if (mysql_real_query(conn, sql, strlen(sql))) {
                //              printf("..update failed! %s\n",mysql_error(conn));
        }
        if (sim) {
                sprintf(
                                sql,
                                "insert into sim(s_id,sim_s_id,sim) values(%d,%d,%d) on duplicate key update  sim_s_id=%d,sim=%d",
                                solution_id, sim_s_id, sim, sim_s_id, sim);
                //      printf("sql= %s\n",sql);
                if (mysql_real_query(conn, sql, strlen(sql))) {
                        //              printf("..update failed! %s\n",mysql_error(conn));
                }

        }

}
void _update_solution_http(int solution_id, int result, int time, int memory,int sim, int sim_s_id,double pass_rate) {
        const char  * cmd=" wget --post-data=\"update_solution=1&sid=%d&result=%d&time=%d&memory=%d&sim=%d&simid=%d&pass_rate=%f\" --load-cookies=cookie --save-cookies=cookie --keep-session-cookies -q -O - \"%s/admin/problem_judge.php\"";
        FILE * fjobs=read_cmd_output(cmd,solution_id,result,  time,  memory, sim, sim_s_id,pass_rate,http_baseurl);
                //fscanf(fjobs,"%d",&ret);
        pclose(fjobs);
}
void update_solution(int solution_id, int result, int time, int memory,int sim, int sim_s_id,double pass_rate) {
        if(result==OJ_TL&&memory==0) result=OJ_ML;
        if(http_judge){
                _update_solution_http( solution_id,  result,  time,  memory, sim, sim_s_id,pass_rate);
        }else{
                _update_solution_mysql( solution_id,  result,  time,  memory, sim, sim_s_id,pass_rate);
        }
}
/* write compile error message back to database */
void _addceinfo_mysql(int solution_id) {
        char sql[(1 << 16)], *end;
        char ceinfo[(1 << 16)], *cend;
        FILE *fp = fopen("ce.txt", "r");
        snprintf(sql, (1 << 16) - 1,
                        "DELETE FROM compileinfo WHERE solution_id=%d", solution_id);
        mysql_real_query(conn, sql, strlen(sql));
        cend = ceinfo;
        while (fgets(cend, 1024, fp)) {
                cend += strlen(cend);
                if (cend - ceinfo > 40000)
                        break;
        }
        cend = 0;
        end = sql;
        strcpy(end, "INSERT INTO compileinfo VALUES(");
        end += strlen(sql);
        *end++ = '\'';
        end += sprintf(end, "%d", solution_id);
        *end++ = '\'';
        *end++ = ',';
        *end++ = '\'';
        end += mysql_real_escape_string(conn, end, ceinfo, strlen(ceinfo));
        *end++ = '\'';
        *end++ = ')';
        *end = 0;
        //      printf("%s\n",ceinfo);
        if (mysql_real_query(conn, sql, end - sql))
                printf("%s\n", mysql_error(conn));
        fclose(fp);
}

// urlencoded function copied from http://www.geekhideout.com/urlcode.shtml
/* Converts a hex character to its integer value */
char from_hex(char ch) {
  return isdigit(ch) ? ch - '0' : tolower(ch) - 'a' + 10;
}

/* Converts an integer value to its hex character*/
char to_hex(char code) {
  static char hex[] = "0123456789abcdef";
  return hex[code & 15];
}

/* Returns a url-encoded version of str */
/* IMPORTANT: be sure to free() the returned string after use */
char *url_encode(char *str) {
  char *pstr = str, *buf =(char *) malloc(strlen(str) * 3 + 1), *pbuf = buf;
  while (*pstr) {
    if (isalnum(*pstr) || *pstr == '-' || *pstr == '_' || *pstr == '.' || *pstr == '~')
      *pbuf++ = *pstr;
    else if (*pstr == ' ')
      *pbuf++ = '+';
    else
      *pbuf++ = '%', *pbuf++ = to_hex(*pstr >> 4), *pbuf++ = to_hex(*pstr & 15);
    pstr++;
  }
  *pbuf = '\0';
  return buf;
}


void _addceinfo_http(int solution_id) {

        char ceinfo[(1 << 16)], *cend;
        char * ceinfo_encode;
        FILE *fp = fopen("ce.txt", "r");

        cend = ceinfo;
        while (fgets(cend, 1024, fp)) {
                cend += strlen(cend);
                if (cend - ceinfo > 40000)
                        break;
        }
        fclose(fp);
        ceinfo_encode=url_encode(ceinfo);
        FILE * ce=fopen("ce.post","w");
        fprintf(ce,"addceinfo=1&sid=%d&ceinfo=%s",solution_id,ceinfo_encode);
        fclose(ce);
        free(ceinfo_encode);

        const char  * cmd=" wget --post-file=\"ce.post\" --load-cookies=cookie --save-cookies=cookie --keep-session-cookies -q -O - \"%s/admin/problem_judge.php\"";
        FILE * fjobs=read_cmd_output(cmd,http_baseurl);
                //fscanf(fjobs,"%d",&ret);
        pclose(fjobs);


}
void addceinfo(int solution_id) {
        if(http_judge){
                _addceinfo_http(solution_id);
        }else{
                _addceinfo_mysql(solution_id);
        }
}
/* write runtime error message back to database */
void _addreinfo_mysql(int solution_id) {
        char sql[(1 << 16)], *end;
        char reinfo[(1 << 16)], *rend;
        FILE *fp = fopen("error.out", "r");
        snprintf(sql, (1 << 16) - 1,
                        "DELETE FROM runtimeinfo WHERE solution_id=%d", solution_id);
        mysql_real_query(conn, sql, strlen(sql));
        rend = reinfo;
        while (fgets(rend, 1024, fp)) {
                rend += strlen(rend);
                if (rend - reinfo > 40000)
                        break;
        }
        rend = 0;
        end = sql;
        strcpy(end, "INSERT INTO runtimeinfo VALUES(");
        end += strlen(sql);
        *end++ = '\'';
        end += sprintf(end, "%d", solution_id);
        *end++ = '\'';
        *end++ = ',';
        *end++ = '\'';
        end += mysql_real_escape_string(conn, end, reinfo, strlen(reinfo));
        *end++ = '\'';
        *end++ = ')';
        *end = 0;
        //      printf("%s\n",ceinfo);
        if (mysql_real_query(conn, sql, end - sql))
                printf("%s\n", mysql_error(conn));
        fclose(fp);
}

void _addreinfo_http(int solution_id) {

        char reinfo[(1 << 16)], *rend;
        char * reinfo_encode;
        FILE *fp = fopen("error.out", "r");

        rend = reinfo;
        while (fgets(rend, 1024, fp)) {
                rend += strlen(rend);
                if (rend - reinfo > 40000)
                        break;
        }
        fclose(fp);
        reinfo_encode=url_encode(reinfo);
        FILE * re=fopen("re.post","w");
        fprintf(re,"addreinfo=1&sid=%d&reinfo=%s",solution_id,reinfo_encode);
        fclose(re);
        free(reinfo_encode);

        const char  * cmd=" wget --post-file=\"re.post\" --load-cookies=cookie --save-cookies=cookie --keep-session-cookies -q -O - \"%s/admin/problem_judge.php\"";
        FILE * fjobs=read_cmd_output(cmd,http_baseurl);
                //fscanf(fjobs,"%d",&ret);
        pclose(fjobs);


}
void addreinfo(int solution_id) {
        if(http_judge){
                _addreinfo_http(solution_id);
        }else{
                _addreinfo_mysql(solution_id);
        }
}
void _update_user_mysql(char * user_id) {
        char sql[BUFFER_SIZE];
        sprintf(
                        sql,
                        "UPDATE `users` SET `solved`=(SELECT count(DISTINCT `problem_id`) FROM `solution` WHERE `user_id`=\'%s\' AND `result`=\'4\') WHERE `user_id`=\'%s\'",
                        user_id, user_id);
        if (mysql_real_query(conn, sql, strlen(sql)))
                write_log(mysql_error(conn));
        sprintf(
                        sql,
                        "UPDATE `users` SET `submit`=(SELECT count(*) FROM `solution` WHERE `user_id`=\'%s\') WHERE `user_id`=\'%s\'",
                        user_id, user_id);
        if (mysql_real_query(conn, sql, strlen(sql)))
                write_log(mysql_error(conn));
}
void _update_user_http(char * user_id) {

        const char  * cmd=" wget --post-data=\"updateuser=1&user_id=%s\" --load-cookies=cookie --save-cookies=cookie --keep-session-cookies -q -O - \"%s/admin/problem_judge.php\"";
        FILE * fjobs=read_cmd_output(cmd,user_id,http_baseurl);
                //fscanf(fjobs,"%d",&ret);
        pclose(fjobs);
}
void update_user(char  * user_id) {
        if(http_judge){
                _update_user_http(user_id);
        }else{
                _update_user_mysql(user_id);
        }
}

void _update_problem_http(int pid) {
        const char  * cmd=" wget --post-data=\"updateproblem=1&pid=%d\" --load-cookies=cookie --save-cookies=cookie --keep-session-cookies -q -O - \"%s/admin/problem_judge.php\"";
        FILE * fjobs=read_cmd_output(cmd,pid,http_baseurl);
                //fscanf(fjobs,"%d",&ret);
        pclose(fjobs);
}
void _update_problem_mysql(int p_id) {
        char sql[BUFFER_SIZE];
        sprintf(
                        sql,
                        "UPDATE `problem` SET `accepted`=(SELECT count(*) FROM `solution` WHERE `problem_id`=\'%d\' AND `result`=\'4\') WHERE `problem_id`=\'%d\'",
                        p_id, p_id);
        if (mysql_real_query(conn, sql, strlen(sql)))
                write_log(mysql_error(conn));
        sprintf(
                        sql,
                        "UPDATE `problem` SET `submit`=(SELECT count(*) FROM `solution` WHERE `problem_id`=\'%d\') WHERE `problem_id`=\'%d\'",
                        p_id, p_id);
        if (mysql_real_query(conn, sql, strlen(sql)))
                write_log(mysql_error(conn));
}
void update_problem(int pid) {
        if(http_judge){
                _update_problem_http(pid);
        }else{
                _update_problem_mysql(pid);
        }
}
int compile(int lang) {
        int pid;

        const char * CP_C[] = { "gcc", "Main.c", "-o", "Main", "-O2","-Wall", "-lm",
                        "--static", "-std=c99", "-DONLINE_JUDGE", NULL };
        const char * CP_X[] = { "g++", "Main.cc", "-o", "Main","-O2", "-Wall",
                        "-lm", "--static", "-DONLINE_JUDGE", NULL };
        const char * CP_P[] = { "fpc", "Main.pas", "-O2","-Co", "-Ct","-Ci", NULL };
//      const char * CP_J[] = { "javac", "-J-Xms32m", "-J-Xmx256m", "Main.java",NULL };

        const char * CP_R[] = { "ruby", "-c", "Main.rb", NULL };
        const char * CP_B[] = { "chmod", "+rx", "Main.sh", NULL };
        const char * CP_Y[] = { "python","-c","import py_compile; py_compile.compile(r'Main.py')", NULL };
        const char * CP_PH[] = { "php", "-l","Main.php", NULL };
    const char * CP_PL[] = { "perl","-c", "Main.pl", NULL };
    const char * CP_CS[] = { "gmcs","-warn:0", "Main.cs", NULL };

    char javac_buf[4][16];
    char *CP_J[5];
    for(int i=0;i<4;i++) CP_J[i]=javac_buf[i];
        sprintf(CP_J[0],"javac");
        sprintf(CP_J[1],"-J%s",java_xms);
        sprintf(CP_J[2],"-J%s",java_xmx);
        sprintf(CP_J[3],"Main.java");
        CP_J[4]=(char *)NULL;

        pid = fork();
        if (pid == 0) {
                struct rlimit LIM;
                LIM.rlim_max = 60;
                LIM.rlim_cur = 60;
                setrlimit(RLIMIT_CPU, &LIM);

                LIM.rlim_max = 90 * STD_MB;
                LIM.rlim_cur = 90 * STD_MB;
                setrlimit(RLIMIT_FSIZE, &LIM);

                LIM.rlim_max = 1024 * STD_MB;
                LIM.rlim_cur = 1024 * STD_MB;
                setrlimit(RLIMIT_AS, &LIM);
                if (lang != 2) {
                        freopen("ce.txt", "w", stderr);
                        //freopen("/dev/null", "w", stdout);
                } else {
                        freopen("ce.txt", "w", stdout);
                }
                switch (lang) {
                case 0:
                        execvp(CP_C[0], (char * const *) CP_C);
                        break;
                case 1:
                        execvp(CP_X[0], (char * const *) CP_X);
                        break;
                case 2:
                        execvp(CP_P[0], (char * const *) CP_P);
                        break;
                case 3:
                        execvp(CP_J[0], (char * const *) CP_J);
                        break;
                case 4:
                        execvp(CP_R[0], (char * const *) CP_R);
                        break;
                case 5:
                        execvp(CP_B[0], (char * const *) CP_B);
                        break;
                case 6:
                        execvp(CP_Y[0], (char * const *) CP_Y);
                        break;
                case 7:
                        execvp(CP_PH[0], (char * const *) CP_PH);
                        break;
                case 8:
                        execvp(CP_PL[0], (char * const *) CP_PL);
                        break;
                case 9:
                        execvp(CP_CS[0], (char * const *) CP_CS);
                        break;
                default:
                        printf("nothing to do!\n");
                }
                if (DEBUG)
                        printf("compile end!\n");
                //exit(!system("cat ce.txt"));
                exit(0);
        } else {
                int status=0;

                waitpid(pid, &status, 0);
                if(lang>3&&lang<7)
                        status=get_file_size("ce.txt");
                if (DEBUG)
                        printf("status=%d\n", status);
                return status;
        }

}
/*
 int read_proc_statm(int pid){
 FILE * pf;
 char fn[4096];
 int ret;
 sprintf(fn,"/proc/%d/statm",pid);
 pf=fopen(fn,"r");
 fscanf(pf,"%d",&ret);
 fclose(pf);
 return ret;
 }
 */
int get_proc_status(int pid, const char * mark) {
        FILE * pf;
        char fn[BUFFER_SIZE], buf[BUFFER_SIZE];
        int ret = 0;
        sprintf(fn, "/proc/%d/status", pid);
        pf = fopen(fn, "r");
        int m = strlen(mark);
        while (pf && fgets(buf, BUFFER_SIZE - 1, pf)) {

                buf[strlen(buf) - 1] = 0;
                if (strncmp(buf, mark, m) == 0) {
                        sscanf(buf + m + 1, "%d", &ret);
                }
        }
        if (pf)
                fclose(pf);
        return ret;
}
int init_mysql_conn() {


        conn = mysql_init(NULL);
        //mysql_real_connect(conn,host_name,user_name,password,db_name,port_number,0,0);
        const char timeout = 30;
        mysql_options(conn, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);

        if (!mysql_real_connect(conn, host_name, user_name, password, db_name,
                        port_number, 0, 0)) {
                write_log("%s", mysql_error(conn));
                return 0;
        }
        const char * utf8sql = "set names utf8";
        if (mysql_real_query(conn, utf8sql, strlen(utf8sql))) {
                write_log("%s", mysql_error(conn));
                return 0;
        }
        return 1;
}
void _get_solution_mysql(int solution_id, char * work_dir, int lang) {
        char sql[BUFFER_SIZE], src_pth[BUFFER_SIZE];
        // get the source code
        MYSQL_RES *res;
        MYSQL_ROW row;
        sprintf(sql, "SELECT source FROM source_code WHERE solution_id=%d",
                        solution_id);
        mysql_real_query(conn, sql, strlen(sql));
        res = mysql_store_result(conn);
        row = mysql_fetch_row(res);


        // create the src file
        sprintf(src_pth, "Main.%s", lang_ext[lang]);
        if (DEBUG)
                printf("Main=%s", src_pth);
        FILE *fp_src = fopen(src_pth, "w");
        fprintf(fp_src, "%s", row[0]);
        mysql_free_result(res);
        fclose(fp_src);
}
void _get_solution_http(int solution_id, char * work_dir, int lang) {
        char  src_pth[BUFFER_SIZE];

        // create the src file
        sprintf(src_pth, "Main.%s", lang_ext[lang]);
        if (DEBUG)
                printf("Main=%s", src_pth);

        //login();

        const char  * cmd2="wget --post-data=\"getsolution=1&sid=%d\" --load-cookies=cookie --save-cookies=cookie --keep-session-cookies -q -O %s \"%s/admin/problem_judge.php\"";
        FILE * pout=read_cmd_output(cmd2,solution_id,src_pth,http_baseurl);

        pclose(pout);

}
void get_solution(int solution_id, char * work_dir, int lang) {
        if(http_judge){
                _get_solution_http(solution_id,  work_dir, lang) ;
        }else{
                _get_solution_mysql(solution_id, work_dir, lang) ;
        }


}
void _get_solution_info_mysql(int solution_id, int & p_id, char * user_id, int & lang) {
        MYSQL_RES *res;
        MYSQL_ROW row;

        char sql[BUFFER_SIZE];
        // get the problem id and user id from Table:solution
        sprintf(
                        sql,
                        "SELECT problem_id, user_id, language FROM solution where solution_id=%d",
                        solution_id);
        //printf("%s\n",sql);
        mysql_real_query(conn, sql, strlen(sql));
        res = mysql_store_result(conn);
        row = mysql_fetch_row(res);
        p_id = atoi(row[0]);
        strcpy(user_id, row[1]);
        lang = atoi(row[2]);
        mysql_free_result(res);
}
void _get_solution_info_http(int solution_id, int & p_id, char * user_id, int & lang) {

        login();

        const char  * cmd="wget --post-data=\"getsolutioninfo=1&sid=%d\" --load-cookies=cookie --save-cookies=cookie --keep-session-cookies -q -O - \"%s/admin/problem_judge.php\"";
        FILE * pout=read_cmd_output(cmd,solution_id,http_baseurl);
        fscanf(pout,"%d",&p_id);
        fscanf(pout,"%s",user_id);
        fscanf(pout,"%d",&lang);
        pclose(pout);

}
void get_solution_info(int solution_id, int & p_id, char * user_id, int & lang) {

        if(http_judge){
                _get_solution_info_http(solution_id,p_id,user_id,lang);
    }else{
                _get_solution_info_mysql(solution_id,p_id,user_id,lang);
        }
}


void _get_problem_info_mysql(int p_id, int & time_lmt, int & mem_lmt, int & isspj) {
        // get the problem info from Table:problem
        char sql[BUFFER_SIZE];
        MYSQL_RES *res;
        MYSQL_ROW row;
        sprintf(
                        sql,
                        "SELECT time_limit,memory_limit,spj FROM problem where problem_id=%d",
                        p_id);
        mysql_real_query(conn, sql, strlen(sql));
        res = mysql_store_result(conn);
        row = mysql_fetch_row(res);
        time_lmt = atoi(row[0]);
        mem_lmt = atoi(row[1]);
        isspj = row[2][0] - '0';
        mysql_free_result(res);
}

void _get_problem_info_http(int p_id, int & time_lmt, int & mem_lmt, int & isspj) {
        //login();

        const char  * cmd="wget --post-data=\"getprobleminfo=1&pid=%d\" --load-cookies=cookie --save-cookies=cookie --keep-session-cookies -q -O - \"%s/admin/problem_judge.php\"";
        FILE * pout=read_cmd_output(cmd,p_id,http_baseurl);
        fscanf(pout,"%d",&time_lmt);
        fscanf(pout,"%d",&mem_lmt);
        fscanf(pout,"%d",&isspj);
        pclose(pout);
}

void get_problem_info(int p_id, int & time_lmt, int & mem_lmt, int & isspj) {
        if(http_judge){
                _get_problem_info_http(p_id,time_lmt,mem_lmt,isspj);
    }else{
                _get_problem_info_mysql(p_id,time_lmt,mem_lmt,isspj);
        }
}

void prepare_files(char * filename, int namelen, char * infile, int & p_id,
                char * work_dir, char * outfile, char * userfile, int runner_id) {
        //              printf("ACflg=%d %d check a file!\n",ACflg,solution_id);

        char  fname[BUFFER_SIZE];
        strncpy(fname, filename, namelen);
        fname[namelen] = 0;
        sprintf(infile, "%s/data/%d/%s.in", oj_home, p_id, fname);
        execute_cmd("cp %s %sdata.in", infile, work_dir);
        execute_cmd("cp %s/data/%d/*.dic %s/", oj_home, p_id,work_dir);

        sprintf(outfile, "%s/data/%d/%s.out", oj_home, p_id, fname);
        sprintf(userfile, "%s/run%d/user.out", oj_home, runner_id);
}

void copy_shell_runtime(char * work_dir) {


        execute_cmd("mkdir %s/lib", work_dir);
        execute_cmd("mkdir %s/lib64", work_dir);
        execute_cmd("mkdir %s/bin", work_dir);
        execute_cmd("cp /lib/* %s/lib/", work_dir);
        execute_cmd("cp /lib/i386-linux-gnu/* %s/lib/", work_dir);
        execute_cmd("cp /lib64/* %s/lib64/", work_dir);
        execute_cmd("cp -a /lib32 %s/", work_dir);
        execute_cmd("cp /bin/busybox %s/bin/", work_dir);
        execute_cmd("ln -s /bin/busybox %s/bin/sh", work_dir);
        execute_cmd("cp /bin/bash %s/bin/bash", work_dir);


}
void copy_bash_runtime(char * work_dir) {
        //char cmd[BUFFER_SIZE];
        //const char * ruby_run="/usr/bin/ruby";
        copy_shell_runtime(work_dir);
        execute_cmd("cp `which bc`  %s/bin/", work_dir);
        execute_cmd("busybox dos2unix Main.sh", work_dir);
        execute_cmd("ln -s /bin/busybox %s/bin/grep", work_dir);
        execute_cmd("ln -s /bin/busybox %s/bin/awk", work_dir);
        execute_cmd("cp /bin/sed %s/bin/sed", work_dir);
        execute_cmd("ln -s /bin/busybox %s/bin/cut", work_dir);
        execute_cmd("ln -s /bin/busybox %s/bin/sort", work_dir);
        execute_cmd("ln -s /bin/busybox %s/bin/join", work_dir);
        execute_cmd("ln -s /bin/busybox %s/bin/wc", work_dir);
        execute_cmd("ln -s /bin/busybox %s/bin/tr", work_dir);
        execute_cmd("ln -s /bin/busybox %s/bin/dc", work_dir);
        execute_cmd("ln -s /bin/busybox %s/bin/dd", work_dir);
        execute_cmd("ln -s /bin/busybox %s/bin/cat", work_dir);
        execute_cmd("ln -s /bin/busybox %s/bin/tail", work_dir);
        execute_cmd("ln -s /bin/busybox %s/bin/head", work_dir);
        execute_cmd("ln -s /bin/busybox %s/bin/xargs", work_dir);
    execute_cmd("chmod +rx %s/Main.sh", work_dir);

}
void copy_ruby_runtime(char * work_dir) {

        copy_shell_runtime(work_dir);
        execute_cmd("mkdir %s/usr", work_dir);
        execute_cmd("mkdir %s/usr/lib", work_dir);
        execute_cmd("cp /usr/lib/libruby* %s/usr/lib/", work_dir);
        execute_cmd("cp /usr/bin/ruby* %s/", work_dir);

}
void copy_python_runtime(char * work_dir) {

        copy_shell_runtime(work_dir);
        execute_cmd("mkdir %s/usr", work_dir);
        execute_cmd("mkdir %s/usr/lib", work_dir);
        execute_cmd("cp /usr/bin/python* %s/", work_dir);

}
void copy_php_runtime(char * work_dir) {

        copy_shell_runtime(work_dir);
        execute_cmd("mkdir %s/usr", work_dir);
        execute_cmd("mkdir %s/usr/lib", work_dir);
        execute_cmd("cp /usr/lib/libedit* %s/usr/lib/", work_dir);
        execute_cmd("cp /usr/lib/libdb* %s/usr/lib/", work_dir);
        execute_cmd("cp /usr/lib/libgssapi_krb5* %s/usr/lib/", work_dir);
        execute_cmd("cp /usr/lib/libkrb5* %s/usr/lib/", work_dir);
        execute_cmd("cp /usr/lib/libk5crypto* %s/usr/lib/", work_dir);
        execute_cmd("cp /usr/lib/libxml2* %s/usr/lib/", work_dir);
        execute_cmd("cp /usr/bin/php* %s/", work_dir);
        execute_cmd("chmod +rx %s/Main.php", work_dir);

}
void copy_perl_runtime(char * work_dir) {

        copy_shell_runtime(work_dir);
        execute_cmd("mkdir %s/usr", work_dir);
        execute_cmd("mkdir %s/usr/lib", work_dir);
        execute_cmd("cp /usr/lib/libperl* %s/usr/lib/", work_dir);
        execute_cmd("cp /usr/bin/perl* %s/", work_dir);

}
void copy_mono_runtime(char * work_dir) {

        copy_shell_runtime(work_dir);
        execute_cmd("mkdir %s/usr", work_dir);
        execute_cmd("mkdir %s/proc", work_dir);
        execute_cmd("mkdir -p %s/usr/lib/mono/2.0", work_dir);

        execute_cmd("cp -a /usr/lib/mono %s/usr/lib/", work_dir);

        execute_cmd("cp /usr/lib/libgthread* %s/usr/lib/", work_dir);

        execute_cmd("mount -o bind /proc %s/proc", work_dir);
        execute_cmd("cp /usr/bin/mono* %s/", work_dir);

        execute_cmd("cp /usr/lib/libgthread* %s/usr/lib/", work_dir);
        execute_cmd("cp /lib/libglib* %s/lib/", work_dir);
        execute_cmd("cp /lib/tls/i686/cmov/lib* %s/lib/tls/i686/cmov/", work_dir);
        execute_cmd("cp /lib/libpcre* %s/lib/", work_dir);
        execute_cmd("cp /lib/ld-linux* %s/lib/", work_dir);
        execute_cmd("cp /lib64/ld-linux* %s/lib64/", work_dir);
        execute_cmd("mkdir -p %s/home/judge", work_dir);
        execute_cmd("chown judge %s/home/judge", work_dir);
        execute_cmd("mkdir -p %s/etc", work_dir);
        execute_cmd("grep judge /etc/passwd>%s/etc/passwd", work_dir);


}

void run_solution(int & lang, char * work_dir, int & time_lmt, int & usedtime,
                int & mem_lmt) {
        nice(19);
                // now the user is "judger"

//      char java_p1[BUFFER_SIZE], java_p2[BUFFER_SIZE];
        // child
        // set the limit
        struct rlimit LIM; // time limit, file limit& memory limit
        // time limit
        if(oi_mode)
                LIM.rlim_cur = time_lmt+1;
        else
                LIM.rlim_cur = (time_lmt - usedtime / 1000) + 1;
        LIM.rlim_max = LIM.rlim_cur;
        //if(DEBUG) printf("LIM_CPU=%d",(int)(LIM.rlim_cur));
        setrlimit(RLIMIT_CPU, &LIM);
        alarm(0);
        alarm(time_lmt*10);

        // file limit
        LIM.rlim_max = STD_F_LIM + STD_MB;
        LIM.rlim_cur = STD_F_LIM;
        setrlimit(RLIMIT_FSIZE, &LIM);
        // proc limit
        switch(lang){
    case 3:  //java
        LIM.rlim_cur=LIM.rlim_max=50;
        break;
    case 5: //bash
                LIM.rlim_cur=LIM.rlim_max=20;
                break;
    case 9: //C#
       LIM.rlim_cur=LIM.rlim_max=3;
       break;
    default:
      LIM.rlim_cur=LIM.rlim_max=1;
        }

    setrlimit(RLIMIT_NPROC, &LIM);

        // set the stack
        LIM.rlim_cur = STD_MB << 6;
        LIM.rlim_max = STD_MB << 6;
        setrlimit(RLIMIT_STACK, &LIM);
        // set the memory
        LIM.rlim_cur = STD_MB *mem_lmt/2*3;
        LIM.rlim_max = STD_MB *mem_lmt*2;
        if(lang<3)
                setrlimit(RLIMIT_AS, &LIM);

        chdir(work_dir);
        // open the files
        freopen("data.in", "r", stdin);
        freopen("user.out", "w", stdout);
        freopen("error.out", "a+", stderr);
        // trace me
        ptrace(PTRACE_TRACEME, 0, NULL, NULL);
        // run me
        if (lang != 3)
                chroot(work_dir);

        while(setgid(1536)!=0) sleep(1);
        while(setuid(1536)!=0) sleep(1);
        while(setresuid(1536, 1536, 1536)!=0) sleep(1);

        switch (lang) {
        case 0:
        case 1:
        case 2:
                execl("./Main", "./Main", (char *)NULL);
                break;
        case 3:
//              sprintf(java_p1, "-Xms%dM", mem_lmt / 2);
//              sprintf(java_p2, "-Xmx%dM", mem_lmt);

                execl("/usr/bin/java", "/usr/bin/java", java_xms,java_xmx,
                                "-Djava.security.manager",
                                "-Djava.security.policy=./java.policy", "Main", (char *)NULL);
                break;
        case 4:
                //system("/ruby Main.rb<data.in");
                execl("/ruby","/ruby", "Main.rb", (char *)NULL);
                break;
        case 5: //bash
                execl("/bin/bash","/bin/bash" ,"Main.sh",(char *)NULL);
                break;
        case 6: //Python
                execl("/python","/python","Main.py",(char *)NULL);
                break;
        case 7: //php
                execl("/php","/php","Main.php",(char *)NULL);
                break;
        case 8: //perl
                execl("/perl","/perl","Main.pl",(char *)NULL);
                break;
        case 9: //Mono C#
                execl("/mono","/mono","--debug","Main.exe",(char *)NULL);
        break;
        }
        //sleep(1);
        exit(0);
}
int fix_java_mis_judge(char *work_dir, int & ACflg, int & topmemory,
                int mem_lmt) {
        int comp_res = OJ_AC;
        if (DEBUG)
                execute_cmd("cat %s/error.out", work_dir);
        comp_res = execute_cmd("grep 'java.lang.OutOfMemoryError'  %s/error.out",
                        work_dir);

        if (!comp_res) {
                printf("JVM need more Memory!");
                ACflg = OJ_ML;
                topmemory = mem_lmt * STD_MB;
        }
        comp_res = execute_cmd("grep 'java.lang.OutOfMemoryError'  %s/user.out",
                        work_dir);

        if (!comp_res) {
                printf("JVM need more Memory or Threads!");
                ACflg = OJ_ML;
                topmemory = mem_lmt * STD_MB;
        }
        comp_res = execute_cmd("grep 'Could not create'  %s/error.out", work_dir);

        if (!comp_res) {
                printf("jvm need more resource,tweak -Xmx(OJ_JAVA_BONUS) Settings");
                ACflg = OJ_RE;
                //topmemory=0;
        }
        return comp_res;
}

void judge_solution(int & ACflg, int & usedtime, int time_lmt, int isspj,
                int p_id, char * infile, char * outfile, char * userfile, int & PEflg,
                int lang, char * work_dir, int & topmemory, int mem_lmt, int solution_id ,double num_of_test, int test_id)  {
        //usedtime-=1000;
        int comp_res;
        if(!oi_mode) num_of_test=1.0;
        if (ACflg == OJ_AC && usedtime > time_lmt * 1000*(num_of_test+1.0))
                ACflg = OJ_TL;
        // compare
        if (ACflg == OJ_AC) {
                if (isspj) {
                        comp_res = execute_cmd("%s/data/%d/spj %s %s %s", oj_home,
                                        p_id, infile, outfile, userfile);

                        if (comp_res == 0)
                                comp_res = OJ_AC;
                        else {
                                if (DEBUG)
                                        printf("fail test %s\n", infile);
                                comp_res = OJ_WA;
                        }
                } else {
                        comp_res = compare(solution_id,test_id,outfile, userfile);
                }
                if (comp_res == OJ_WA) {
                        ACflg = OJ_WA;
                        if (DEBUG)
                                printf("fail test %s\n", infile);
                } else if (comp_res == OJ_PE)
                        PEflg = OJ_PE;
                ACflg = comp_res;
        }
        //jvm popup messages, if don't consider them will get miss-WrongAnswer
        if (lang == 3 && ACflg != OJ_AC) {
                comp_res = fix_java_mis_judge(work_dir, ACflg, topmemory, mem_lmt);
        }
}

int get_page_fault_mem(struct rusage & ruse, pid_t & pidApp) {
        //java use pagefault
        int m_vmpeak, m_vmdata, m_minflt;
        m_minflt = ruse.ru_minflt * getpagesize();
        if (0 && DEBUG) {
                m_vmpeak = get_proc_status(pidApp, "VmPeak:");
                m_vmdata = get_proc_status(pidApp, "VmData:");
                printf("VmPeak:%d KB VmData:%d KB minflt:%d KB\n", m_vmpeak, m_vmdata,
                                m_minflt >> 10);
        }
        return m_minflt;
}
void print_runtimeerror(char * err){
        FILE *ferr=fopen("error.out","a+");
        fprintf(ferr,"Runtime Error:%s\n",err);
        fclose(ferr);
}
void clean_session(pid_t p){
        //char cmd[BUFFER_SIZE];
        const char *pre = "ps awx -o \"\%p \%P\"|grep -w ";
        const char *post = " | awk \'{ print $1  }\'|xargs kill -9";
        execute_cmd("%s %d %s",pre,p,post);
                execute_cmd("ps aux |grep \\^judge|awk '{print $2}'|xargs kill");
}

void watch_solution(pid_t pidApp, char * infile, int & ACflg, int isspj,
                char * userfile, char * outfile, int solution_id, int lang,
                int & topmemory, int mem_lmt, int & usedtime, int time_lmt, int & p_id,
                int & PEflg, char * work_dir) {
        // parent
        int tempmemory;

        if (DEBUG)
                printf("pid=%d judging %s\n", pidApp, infile);

        int status, sig, exitcode;
        struct user_regs_struct reg;
        struct rusage ruse;
        int sub = 0;
        while (1) {
                // check the usage

                wait4(pidApp, &status, 0, &ruse);
                //sig = status >> 8;/*status >> 8 Ã¥Â·Â®Ã¤Â¸ÂÃ¥Â¤Å¡Ã¦ËÂ¯EXITCODE*/

                if (WIFEXITED(status))
                        break;
                if ((lang < 4 || lang == 9) && get_file_size("error.out")&&!oi_mode) {
                        ACflg = OJ_RE;
                        //addreinfo(solution_id);
                        ptrace(PTRACE_KILL, pidApp, NULL, NULL);
                        break;
                }

                if (!isspj && get_file_size(userfile) > get_file_size(outfile) * 2+1024) {
                        ACflg = OJ_OL;
                        ptrace(PTRACE_KILL, pidApp, NULL, NULL);
                        break;
                }

                exitcode = WEXITSTATUS(status);
                /*exitcode == 5 waiting for next CPU allocation          * ruby using system to run,exit 17 ok
                 *  */
                if ((lang >= 3 && exitcode == 17) || exitcode == 0x05 || exitcode == 0)
                        //go on and on
                        ;
                else {

                        if (DEBUG) {
                                printf("status>>8=%d\n", exitcode);

                        }
                        //psignal(exitcode, NULL);

                        if (ACflg == OJ_AC){
                                switch (exitcode) {
                                        case SIGCHLD:
                                        case SIGALRM:
               alarm(0);
                                        case SIGKILL:
                                        case SIGXCPU:
                                                ACflg = OJ_TL;
                                                break;
                                        case SIGXFSZ:
                                                ACflg = OJ_OL;
                                                break;
                                        default:
                                                ACflg = OJ_RE;
                                }
                                print_runtimeerror(strsignal(exitcode));
                        }
                        ptrace(PTRACE_KILL, pidApp, NULL, NULL);

                        break;
                }
                if (WIFSIGNALED(status)) {
                        /*  WIFSIGNALED: if the process is terminated by signal
                         *
                         *  psignal(int sig, char *s)，like perror(char *s)，print out s, with error msg from system of sig
       * sig = 5 means Trace/breakpoint trap
       * sig = 11 means Segmentation fault
       * sig = 25 means File size limit exceeded
       */
                        sig = WTERMSIG(status);

                        if (DEBUG) {
                                printf("WTERMSIG=%d\n", sig);
                                psignal(sig, NULL);
                        }
                        if (ACflg == OJ_AC){
                                switch (sig) {
                                case SIGCHLD:
                                case SIGALRM:
             alarm(0);
                                case SIGKILL:
                                case SIGXCPU:
                                        ACflg = OJ_TL;
                                        break;
                                case SIGXFSZ:
                                        ACflg = OJ_OL;
                                        break;

                                default:
                                        ACflg = OJ_RE;
                                }
                                print_runtimeerror(strsignal(sig));
                        }
                        break;
                }
                /*     comment from http://www.felix021.com/blog/read.php?1662

  WIFSTOPPED: return true if the process is paused or stopped while ptrace is watching on it
  WSTOPSIG: get the signal if it was stopped by signal
                 */

                // check the system calls
                ptrace(PTRACE_GETREGS, pidApp, NULL, &reg);

                if (call_counter[reg.REG_SYSCALL] == 0) { //do not limit JVM syscall for using different JVM
                        ACflg = OJ_RE;

                        char error[BUFFER_SIZE];
                        sprintf(error,"[ERROR] A Not allowed system call: runid:%d callid:%ld\n",
                                        solution_id, reg.REG_SYSCALL);
                        write_log(error);
                        print_runtimeerror(error);
                        ptrace(PTRACE_KILL, pidApp, NULL, NULL);
                } else {
                        if (sub == 1 && call_counter[reg.REG_SYSCALL] > 0)
                                call_counter[reg.REG_SYSCALL]--;
                }
                sub = 1 - sub;

                //jvm gc ask VM before need,so used kernel page fault times and page size
                if (lang == 3) {
                        tempmemory = get_page_fault_mem(ruse, pidApp);
                } else {//other use VmPeak
                        tempmemory = get_proc_status(pidApp, "VmPeak:") << 10;
                }
                if (tempmemory > topmemory)
                        topmemory = tempmemory;
                if (topmemory > mem_lmt * STD_MB) {
                        if (DEBUG)
                                printf("out of memory %d\n", topmemory);
                        if (ACflg == OJ_AC)
                                ACflg = OJ_ML;
                        ptrace(PTRACE_KILL, pidApp, NULL, NULL);
                        break;
                }
                ptrace(PTRACE_SYSCALL, pidApp, NULL, NULL);
        }
        usedtime += (ruse.ru_utime.tv_sec * 1000 + ruse.ru_utime.tv_usec / 1000);
        usedtime += (ruse.ru_stime.tv_sec * 1000 + ruse.ru_stime.tv_usec / 1000);

        //clean_session(pidApp);
}
void clean_workdir(char * work_dir ) {
        if (DEBUG) {
                execute_cmd("mv %s/* %slog/", work_dir, work_dir);
        } else {
                execute_cmd("umount %s/proc", work_dir);
                execute_cmd("rm -Rf %s/*", work_dir);

        }

}

void init_parameters(int argc, char ** argv, int & solution_id,int & runner_id) {
        if (argc < 3) {
                fprintf(stderr, "Usage:%s solution_id runner_id.\n", argv[0]);
                fprintf(stderr, "Multi:%s solution_id runner_id judge_base_path.\n",
                                argv[0]);
                fprintf(stderr,
                                "Debug:%s solution_id runner_id judge_base_path debug.\n",
                                argv[0]);
                exit(1);
        }
        DEBUG = (argc > 4);
        if (argc > 3)
                //if (strcmp(argv[3],"debug")==0)
				//	DEBUG=true;
				//else
					strcpy(oj_home, argv[3]);
        else
                strcpy(oj_home, "/home/judge");

        chdir(oj_home); // change the dir// init our work

        solution_id = atoi(argv[1]);
        runner_id = atoi(argv[2]);
}
int get_sim(int solution_id, int lang, int pid, int &sim_s_id) {
        char src_pth[BUFFER_SIZE];
        //char cmd[BUFFER_SIZE];
        sprintf(src_pth, "Main.%s", lang_ext[lang]);

        int sim = execute_cmd("sim.sh %s %d", src_pth, pid);
        if (!sim) {
                execute_cmd("mkdir ../data/%d/ac/", pid);

                execute_cmd("mv %s ../data/%d/ac/%d.%s", src_pth, pid, solution_id,
                                lang_ext[lang]);
                //c cpp will
                if(lang==0)execute_cmd("ln ../data/%d/ac/%d.%s ../data/%d/ac/%d.%s", pid, solution_id,
                                lang_ext[lang], pid, solution_id,lang_ext[lang+1]);
                if(lang==1)execute_cmd("ln ../data/%d/ac/%d.%s ../data/%d/ac/%d.%s", pid, solution_id,
                                lang_ext[lang], pid, solution_id,lang_ext[lang-1]);


        } else {

                FILE * pf;
                pf = fopen("sim", "r");
                if (pf) {
                        fscanf(pf, "%d%d", &sim, &sim_s_id);
                        fclose(pf);
                }

        }
        if(solution_id<=sim_s_id) sim=0;
        return sim;
}
void mk_shm_workdir(char * work_dir){
        char shm_path[BUFFER_SIZE];
        sprintf(shm_path,"/dev/shm/hustoj/%s",work_dir);
        execute_cmd("mkdir -p %s",shm_path);
        execute_cmd("rm -rf %s",work_dir);
        execute_cmd("ln -s %s %s/",shm_path,oj_home);
  execute_cmd("chown judge %s ",shm_path);
  execute_cmd("chmod 775 %s ",shm_path);
  //sim need a soft link in shm_dir to work correctly
  sprintf(shm_path,"/dev/shm/hustoj/%s/",oj_home);
  execute_cmd("ln -s %s/data %s",oj_home,shm_path);

}
int count_in_files(char * dirpath){
        const char  * cmd="ls -l %s/*.in|wc -l";
        int ret=0;
        FILE * fjobs=read_cmd_output(cmd,dirpath);
        fscanf(fjobs,"%d",&ret);
        pclose(fjobs);

        return ret;
}

int get_test_file(char* work_dir,int p_id){
        char filename[BUFFER_SIZE];
        char localfile[BUFFER_SIZE];
        int ret=0;
        const char  * cmd=" wget --post-data=\"gettestdatalist=1&pid=%d\" --load-cookies=cookie --save-cookies=cookie --keep-session-cookies -q -O - \"%s/admin/problem_judge.php\"";
        FILE * fjobs=read_cmd_output(cmd,p_id,http_baseurl);
        while(fgets(filename,BUFFER_SIZE-1,fjobs)!=NULL){
                sscanf(filename,"%s",filename);
                sprintf(localfile,"%s/data/%d/%s",oj_home,p_id,filename);
                if(DEBUG) printf("localfile[%s]\n",localfile);
                if(access(localfile,0)==-1){
                        if(strcmp(filename,"spj")==0) continue;
                        execute_cmd("mkdir -p %s/data/%d",oj_home,p_id);
                        const char * cmd2=" wget --post-data=\"gettestdata=1&filename=%d/%s\" --load-cookies=cookie --save-cookies=cookie --keep-session-cookies -q -O \"%s\"  \"%s/admin/problem_judge.php\"";
                        execute_cmd(cmd2,p_id,filename,localfile,http_baseurl);
                        ret++;

                        if(strcmp(filename,"spj.c")==0){
                         //   sprintf(localfile,"%s/data/%d/spj.c",oj_home,p_id);
                            if(access(localfile,0)==0){
                                const char * cmd3="gcc -o %s/data/%d/spj %s/data/%d/spj.c";
                                execute_cmd(cmd3,oj_home,p_id,oj_home,p_id);
                            }

                        }
                        if(strcmp(filename,"spj.cc")==0){
                        //     sprintf(localfile,"%s/data/%d/spj.cc",oj_home,p_id);
                            if(access(localfile,0)==0){
                                const char * cmd4="g++ -o %s/data/%d/spj %s/data/%d/spj.cc";
                                execute_cmd(cmd4,oj_home,p_id,oj_home,p_id);
                            }
                        }
                }

        }
        pclose(fjobs);

        return ret;
}
int main(int argc, char** argv) {

    char work_dir[BUFFER_SIZE];
    //char cmd[BUFFER_SIZE];
    char user_id[BUFFER_SIZE];
    int solution_id = 1000;
    int runner_id = 0;
    int p_id, time_lmt, mem_lmt, lang, isspj, sim, sim_s_id,max_case_time=0,sum_time=0;

    init_parameters(argc, argv, solution_id, runner_id);

    init_mysql_conf();

    if (!http_judge&&!init_mysql_conn()) {
            exit(0); //exit if mysql is down
    }
    //set work directory to start running & judging
    sprintf(work_dir, "%s/run%s/", oj_home, argv[2]);

    if(shm_run) mk_shm_workdir(work_dir);

    chdir(work_dir);
    execute_cmd("rm %s/*",work_dir);

    if(http_judge)
            system("ln -s ../cookie ./");
    get_solution_info(solution_id, p_id, user_id, lang);
    //get the limit
    write_log("Judging solution %d of problem %d by user %s",solution_id, p_id, user_id);

    get_problem_info(p_id, time_lmt, mem_lmt, isspj);
    //copy source file

    get_solution(solution_id, work_dir, lang);

    //java is lucky
    if (lang >= 3) {
            // the limit for java
            time_lmt = time_lmt + java_time_bonus;
            mem_lmt = mem_lmt + java_memory_bonus;
            // copy java.policy
            execute_cmd( "cp %s/etc/java0.policy %sjava.policy", oj_home, work_dir);

    }

    //never bigger than judged set value;
    if (time_lmt > 300 || time_lmt < 1)
            time_lmt = 300;
    if (mem_lmt > 1024 || mem_lmt < 1)
            mem_lmt = 1024;

    if (DEBUG)
            printf("time: %d mem: %d\n", time_lmt, mem_lmt);

    // compile
    //      printf("%s\n",cmd);
    // set the result to compiling
    int Compile_OK;

    Compile_OK = compile(lang);
    if (Compile_OK != 0) {
        update_solution(solution_id, OJ_CE, 0, 0, 0,0, 0.0);
        addceinfo(solution_id);
        update_user(user_id);
        update_problem(p_id);
        if(!http_judge)
                mysql_close(conn);
        if (!DEBUG)
                clean_workdir(work_dir);
        else
                write_log("compile error");
        exit(0);
    } else {
        update_solution(solution_id, OJ_RI, 0, 0, 0,0, 0.0);
    }
    //exit(0);
    // run
    char fullpath[BUFFER_SIZE];
    char infile[BUFFER_SIZE];
    char outfile[BUFFER_SIZE];
    char userfile[BUFFER_SIZE];
    sprintf(fullpath, "%s/data/%d", oj_home, p_id); // the fullpath of data dir
    // open DIRs
    DIR *dp;
    dirent *dirp;
	int test_id=0;
    // using http to get remote test data files
    if (http_judge)
        get_test_file(work_dir,p_id);
    if ((dp = opendir(fullpath)) == NULL) {
        write_log("No such dir:%s!\n", fullpath);
        if(!http_judge)
            mysql_close(conn);
        exit(-1);
    }
    int ACflg, PEflg;
    ACflg = PEflg = OJ_AC;
    int namelen;
    int usedtime = 0, topmemory = 0;

    //create chroot for ruby bash python
    if (lang == 4)
        copy_ruby_runtime(work_dir);
    if (lang == 5)
        copy_bash_runtime(work_dir);
    if (lang == 6)
        copy_python_runtime(work_dir);
    if (lang == 7)
        copy_php_runtime(work_dir);
    if (lang == 8)
        copy_perl_runtime(work_dir);
    if (lang == 9)
        copy_mono_runtime(work_dir);
    // read files and run
    double pass_rate=0.0;
    int num_of_test=0;
    int finalACflg=ACflg;
    for (;(oi_mode|| ACflg == OJ_AC )&& (dirp = readdir(dp)) != NULL;) {
        namelen = isInFile(dirp->d_name); // check if the file is *.in or not
        if (namelen == 0)
            continue;

        prepare_files(dirp->d_name, namelen, infile, p_id, work_dir, outfile, userfile, runner_id);
        init_syscalls_limits(lang);
		test_id=atoi(dirp->d_name);
        pid_t pidApp = fork();
        if (pidApp == 0) {
            run_solution(lang, work_dir, time_lmt, usedtime, mem_lmt);
            sum_time+=usedtime;
        } else {
            watch_solution(pidApp, infile, ACflg, isspj, userfile, outfile, solution_id, lang, topmemory, mem_lmt, usedtime, time_lmt,p_id, PEflg, work_dir);
            judge_solution(ACflg, usedtime, time_lmt, isspj, p_id, infile, outfile, userfile, PEflg, lang, work_dir, topmemory,        mem_lmt, solution_id,num_of_test,test_id);
            sum_time+=usedtime;
            if(use_max_time){
                max_case_time=usedtime>max_case_time?usedtime:max_case_time;
                usedtime=0;
            }
            //clean_session(pidApp);
        }
        if(oi_mode){
            if(ACflg == OJ_AC){
                pass_rate+=1;
                //addrun_test(solution_id,atoi(dirp->d_name),0);
            }else{
                /**/
                if (ACflg == OJ_AC && PEflg == OJ_PE){
                    ACflg = OJ_PE;
                    PEflg=false;
                }
                /**/
                if(ACflg == OJ_RE)addreinfo(solution_id);
                //finalACflg=ACflg;
                //addrun_test(solution_id,test_id,ACflg,dirp->d_name);
                ACflg=OJ_AC;
            }
            num_of_test++;
            ACflg = OJ_AC;
        }
    } //end for
	write_log("Him has %f over %d",pass_rate,num_of_test);
    if (ACflg == OJ_AC && PEflg == OJ_PE)
        ACflg = OJ_PE;
    if (sim_enable && ACflg == OJ_AC &&(!oi_mode||finalACflg==OJ_AC)&& lang < 5) {//bash don't supported
        sim = get_sim(solution_id, lang, p_id, sim_s_id);
    }else{
        sim = 0;
    }
    if(ACflg == OJ_RE)addreinfo(solution_id);
    if(use_max_time){
        usedtime=max_case_time;
    }
    /*
    if(ACflg == OJ_TL){
        usedtime=time_lmt*1000;
    }
    */
    if(oi_mode){
        if(num_of_test>0) pass_rate/=num_of_test;
        update_solution(solution_id, finalACflg, sum_time, topmemory >> 10, sim, sim_s_id,pass_rate);
    }else{
        update_solution(solution_id, ACflg, usedtime, topmemory >> 10, sim, sim_s_id,0);
    }
    update_user(user_id);
    update_problem(p_id);
    clean_workdir(work_dir);
    if (DEBUG)
        write_log("result=%d", oi_mode?finalACflg:ACflg);
    if(!http_judge)
        mysql_close(conn);
    return 0;
}
