#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/wait.h>        
#include <sys/types.h>     
#include <pwd.h>           
#include <limits.h>          
#include <time.h>

#define MAX_CMD 1000         
#define MAX_ARG 1000          
#define HISTORY_COUNT 1000  
#define MAX_BOOKMARKS 1000

// 북마크를 저장할 구조체 정의
typedef struct {
    char name[MAX_CMD];  // 북마크 이름
    char path[PATH_MAX]; // 북마크 경로
} Bookmark;

Bookmark bookmarks[MAX_BOOKMARKS]; // 북마크 배열
int bookmarkCount = 0;              // 현재 저장된 북마크 수

void myPwd();                // 현재 디렉토리 출력 함수
void myCd(char *path);       // 디렉토리 변경 함수
int splitToken(char *command, char *argv[]);  // 명령어를 토큰으로 분리하는 함수
char *getNextToken(char *command);
int hasPipe(char *command, char **part1, char **part2);  // 파이프(|)가 있는지 확인하는 함수
void addHistory(const char *cmd, char history[HISTORY_COUNT][MAX_CMD], int *historyIndex); // 명령어를 히스토리에 추가하는 함수
void printHistory(char history[HISTORY_COUNT][MAX_CMD], int historyIndex); // 히스토리 출력 함수
// 북마크 관련 함수 선언
void addBookmark(const char *name, const char *path);
void deleteBookmark(const char *name);
void listBookmarks();


int main() {
    char cmd[MAX_CMD];  // 사용자 명령어를 저장할 배열
    char *argv[MAX_ARG];  // 명령어 인자들을 저장할 배열
    int pid, status;  // 프로세스 ID와 상태

    // 히스토리 관련 변수들
    char history[HISTORY_COUNT][MAX_CMD];  // 명령어 히스토리
    int historyIndex = 0;  // 현재 히스토리 인덱스
   
    char currentPath[PATH_MAX];  // 현재 경로를 저장할 배열
    char hostname[HOST_NAME_MAX];  // 호스트 이름을 저장할 배열
    char *username;  // 사용자 이름
    struct passwd *pw;  // passwd 구조체
    uid_t uid;  // 사용자 ID

    uid = geteuid();  // 유효 사용자 ID 가져오기
    pw = getpwuid(uid);  // passwd 구조체 가져오기
    if (pw) {
        username = pw->pw_name;  // 사용자 이름 설정
    } else {
        username = "unknown";  // 알 수 없는 사용자 처리
    }

    gethostname(hostname, sizeof(hostname));  // 호스트 이름 가져오기
   
   // 현재 시간을 받아서 인사말 출력
    time_t now = time(NULL);
    struct tm *tm_struct = localtime(&now);

    // 시간에 따른 인사말 결정
    int hour = tm_struct->tm_hour;
    if (hour < 12) {
        printf("Good morning!\n");
    } else if (hour < 18) {
        printf("Good afternoon!\n");
    } else {
        printf("Good evening!\n");
    }
   
    while(1) {
        // 현재 시간을 HH:MM:SS 형태로 출력
        char timeStr[9]; // 시간 문자열
        time_t now = time(NULL);
        struct tm *tm_struct = localtime(&now);
        strftime(timeStr, sizeof(timeStr), "%H:%M:%S", tm_struct);
      
      // 현재 작업 디렉토리 가져오기
        if (getcwd(currentPath, sizeof(currentPath)) == NULL) {
            perror("getcwd");
            return EXIT_FAILURE;
        }
      
      // 프롬프트 출력
        printf("[%s]\033[1;32m%s@%s\033[0m:\033[1;34m%s\033[0m$ ", timeStr, username, hostname, currentPath);
      
      // 사용자 입력 받기
        fgets(cmd, MAX_CMD, stdin);
      
        // Enter만 입력된 경우 무시
        if(cmd[0] == '\n')
            continue;
      
      // 입력받은 명령어를 히스토리에 추가
        addHistory(cmd, history, &historyIndex); 

        char *part1, *part2;  // 파이프를 기준으로 나뉜 명령어 부분
        int fd[2];  // 파일 디스크립터 배열
      
      // 파이프가 있는지 검사
        if (hasPipe(cmd, &part1, &part2)) {
         // 파이프 생성
            if (pipe(fd) == -1) {
                perror("pipe");
                continue;
            }
         
         // 첫 번째 명령어를 처리하기 위한 자식 프로세스 생성
            if ((pid = fork()) == -1) {
                perror("fork");
                continue;
            }

            if (pid == 0) {
                // 자식 프로세스에서 첫 번째 파이프 부분 실행
                close(fd[0]); // 파이프 읽기 끝 닫기
                dup2(fd[1], STDOUT_FILENO); // 표준 출력을 파이프 쓰기 끝으로 복제
                close(fd[1]); // 복제된 후의 원래 파이프 쓰기 끝 닫기

                splitToken(part1, argv); // 첫 번째 명령어 분리
                execvp(argv[0], argv); // 명령어 실행
                perror("execvp");
                exit(EXIT_FAILURE);
            } else {
                // 부모 프로세스
                wait(NULL); // 첫 번째 자식 프로세스가 끝날 때까지 대기

                // 두 번째 명령어를 처리하기 위한 자식 프로세스 생성
                if ((pid = fork()) == -1) {
                    perror("fork");
                    continue;
                }

                if (pid == 0) {
                    // 자식 프로세스에서 두 번째 파이프 부분 실행
                    close(fd[1]); // 파이프 쓰기 끝 닫기
                    dup2(fd[0], STDIN_FILENO); // 표준 입력을 파이프 읽기 끝으로 복제
                    close(fd[0]); // 복제된 후의 원래 파이프 읽기 끝 닫기

                    splitToken(part2, argv); // 두 번째 명령어 분리
                    execvp(argv[0], argv); // 명령어 실행
                    perror("execvp");
                    exit(EXIT_FAILURE);
                } else {
                    // 부모 프로세스에서 파이프 닫기
                    close(fd[0]);
                    close(fd[1]);
                    wait(NULL); // 두 번째 자식 프로세스가 끝날 때까지 대기
                }
            }
        } 
      // 파이프가 없는 경우의 명령어 처리
      else {
            int argc = splitToken(cmd, argv);  // 명령어를 토큰으로 분리하고 인자 개수를 반환
           
         // 'exit' 명령어 처리
            if(!strcmp(argv[0], "exit")) {
                puts("Good Bye");
                break;
            } 
         
         // 'mypwd' 명령어 처리
         else if(!strcmp(argv[0], "mypwd")) {
                myPwd();
                continue;
            } 
         
         // 'cd' 명령어 처리
         else if(!strcmp(argv[0], "cd")) {
                myCd(argc > 1 ? argv[1] : NULL);
                continue;
            } 
         
         // 북마크 관련 명령어 처리
         else if(!strcmp(argv[0], "bookmark")) {
            if (argc == 2 && !strcmp(argv[1], "list")) {
               listBookmarks();
            } else if (argc == 3 && !strcmp(argv[1], "delete")) {
               deleteBookmark(argv[2]);
            } else if (argc == 3) {
               addBookmark(argv[1], argv[2]);
            } else {
               printf("Usage: bookmark list OR bookmark [name] [path] OR bookmark delete [name]\n");
            }
            continue;
         } 
         
         // 'history' 명령어 처리
         else if (!strcmp(argv[0], "history")) {
                printHistory(history, historyIndex);
                continue;
            }
         
            // 외부 명령어 처리를 위한 자식 프로세스 생성
            pid = fork();
            if(pid == 0) {
            // 자식 프로세스에서 외부 명령어 실행
                execvp(argv[0], argv);
                fprintf(stderr, "Failed to execute %s\n", argv[0]);
                exit(EXIT_FAILURE);  // 실패 시 종료
            } else if(pid > 0) {
            // 부모 프로세스에서 자식 프로세스 종료까지 대기
                wait(&status);
            } else {
            // fork 실패 처리
                fprintf(stderr, "Failed to fork\n");
            }
        }
      
      // 명령어 처리 후 argv 배열에 할당된 메모리 해제
        for(int i = 0; i < MAX_ARG; i++) {
            free(argv[i]);
        }
    }

    return 0;
}

// 명령어를 토큰으로 분리하는 함수
int splitToken(char *command, char *argv[]) {
    int argc = 0;  // 인자 개수
    char *token = strtok(command, " \n");  // 공백과 개행 문자를 기준으로 첫 번째 토큰 분리
   
   // 토큰이 NULL이 아니고 최대 인자 개수에 도달하지 않았을 때 반복
    while(token != NULL && argc < MAX_ARG) {
        argv[argc] = strdup(token);  // 분리된 토큰을 argv에 복사
        argc++;  // 인자 개수 증가
        token = strtok(NULL, " \n");  // 다음 토큰 분리
    }

    argv[argc] = NULL;  // argv의 마지막에 NULL 추가
    return argc;  // 인자 개수 반환
}

// 파이프(|)가 있는지 확인하는 함수
int hasPipe(char *command, char **part1, char **part2) {
    char *pipePos = strchr(command, '|');  // 명령어에서 '|' 문자 찾기
    if (pipePos != NULL) {
        *pipePos = '\0';  // 파이프 위치에 NULL 문자 삽입하여 명령어 분리
        *part1 = command;  // 첫 번째 부분 설정
        *part2 = pipePos + 1;  // 두 번째 부분 설정
        return 1;  // 파이프 존재
    }
    return 0;  // 파이프 없음
}

// 디렉토리 변경 함수
void myCd(char *path) {
    if (path == NULL) {
        // 경로가 NULL이면 홈 디렉토리로 이동
        path = getenv("HOME");
    } else {
        // 북마크 이름과 일치하는 경로가 있는지 확인
        for (int i = 0; i < bookmarkCount; i++) {
            if (strcmp(bookmarks[i].name, path) == 0) {
                path = bookmarks[i].path;
                break;
            }
        }
    }

    // 경로 변경, 실패시 오류 메시지 출력
    if (chdir(path) == -1) {
        perror("cd");
    }
}

// 현재 디렉토리 출력 함수
void myPwd() {
    char currentPath[256];  // 현재 경로를 저장할 배열
    
    if (getcwd(currentPath, sizeof(currentPath)) != NULL) {
        printf("%s\n", currentPath);  // 현재 경로 출력
    } else {
        perror("mypwd");  // 에러 발생 시 출력
    }
}

// 명령어를 히스토리에 추가하는 함수
void addHistory(const char *cmd, char history[HISTORY_COUNT][MAX_CMD], int *historyIndex) {
    strncpy(history[*historyIndex % HISTORY_COUNT], cmd, MAX_CMD);  // 명령어를 히스토리에 복사
    (*historyIndex)++;  // 히스토리 인덱스 증가
}

// 명령어 히스토리 출력 함수
void printHistory(char history[HISTORY_COUNT][MAX_CMD], int historyIndex) {
    int start = historyIndex >= HISTORY_COUNT ? historyIndex - HISTORY_COUNT : 0;  // 시작 인덱스 계산
    for (int i = start; i < historyIndex; i++) {
        printf("%d %s", i - start + 1, history[i % HISTORY_COUNT]);  // 히스토리 출력
    }
}

// 새로운 북마크 추가 함수
void addBookmark(const char *name, const char *path) {
    // 북마크 최대 개수를 초과하지 않았는지 확인
    if (bookmarkCount < MAX_BOOKMARKS) {
        // 북마크 정보 저장
        strncpy(bookmarks[bookmarkCount].name, name, MAX_CMD);
        strncpy(bookmarks[bookmarkCount].path, path, PATH_MAX);
        bookmarkCount++;  // 북마크 개수 증가
    } else {
        // 북마크 최대 개수 초과시 오류 메시지 출력
        printf("Bookmark limit reached.\n");
    }
}

// 북마크 삭제 함수
void deleteBookmark(const char *name) {
    int found = 0;
    // 해당 이름의 북마크 찾기
    for (int i = 0; i < bookmarkCount; i++) {
        if (strcmp(bookmarks[i].name, name) == 0) {
            found = 1;
        }
        if (found && i < bookmarkCount - 1) {
            // 찾은 북마크 삭제 및 나머지 북마크 이동
            bookmarks[i] = bookmarks[i + 1];
        }
    }
    // 북마크 삭제 성공 여부에 따른 메시지 출력
    if (found) {
        bookmarkCount--;
        printf("Bookmark '%s' deleted.\n", name);
    } else {
        printf("Bookmark '%s' not found.\n", name);
    }
}

// 북마크 목록 출력 함수
void listBookmarks() {
    // 북마크가 없는 경우 메시지 출력
    if (bookmarkCount == 0) {
        printf("No bookmarks set.\n");
        return;
    }

    // 저장된 북마크 목록 출력
    printf("Bookmarks:\n");
    for (int i = 0; i < bookmarkCount; i++) {
        printf("%s -> %s\n", bookmarks[i].name, bookmarks[i].path);
    }
}
