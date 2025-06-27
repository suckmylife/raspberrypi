#include "sig.h"

// --- 시그널 핸들러 함수 정의 ---
void handle_parent_sigusr(int signum) {
    parent_sigusr_arrived = 1; 
    syslog(LOG_INFO, "Parent: SIGUSR1 received (message from child).");
}

void handle_child_sigusr(int signum) {
    child_sigusr_arrived = 1;
    syslog(LOG_INFO, "Child: SIGUSR1 received (message from parent).");
}

void handle_sigchld_main(int signum) { 
    child_exited_flag = 1; 
    syslog(LOG_INFO, "Parent: SIGCHLD received. Child exited flag set.");
}

void clean_active_process() {
    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) { 
        syslog(LOG_INFO, "Parent: Child %d terminated (status: %d).", pid, status);
        for (int i = 0; i < num_active_children; i++) {
            if (active_children[i].pid == pid) {
                close(active_children[i].parent_to_child_write_fd); 
                close(active_children[i].child_to_parent_read_fd);  
                active_children[i].isActive = false; 
                
                for (int j = i; j < num_active_children - 1; j++) {
                    active_children[j] = active_children[j+1];
                }
                num_active_children--;
                syslog(LOG_INFO, "Parent: Child %d removed from list. Active children: %d.", pid, num_active_children);
                break; 
            }
        }
    }
}

// --- 시그널 핸들러 등록 함수 ---
void setup_signal_handlers_parent_main() { 
    struct sigaction sa_usr, sa_chld;

    sa_usr.sa_handler = handle_parent_sigusr;
    sigemptyset(&sa_usr.sa_mask);
    sa_usr.sa_flags = 0; 
    if (sigaction(SIGUSR1, &sa_usr, NULL) == -1) {
        syslog(LOG_ERR, "Parent: Failed to set SIGUSR1 handler: %m");
        exit(1);
    }
    syslog(LOG_INFO, "Parent: SIGUSR1 handler set for parent.");

    sa_chld.sa_handler = handle_sigchld_main; 
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP; 
    if (sigaction(SIGCHLD, &sa_chld, NULL) == -1) {
        syslog(LOG_ERR, "Parent: Failed to set SIGCHLD handler: %m");
        exit(1);
    }
    syslog(LOG_INFO, "Parent: SIGCHLD handler set for parent.");
}

void setup_signal_handlers_child_main() { 
    struct sigaction sa_usr;

    sa_usr.sa_handler = handle_child_sigusr;
    sigemptyset(&sa_usr.sa_mask);
    sa_usr.sa_flags = 0; 
    if (sigaction(SIGUSR1, &sa_usr, NULL) == -1) {
        syslog(LOG_ERR, "Child: Failed to set SIGUSR1 handler: %m");
        exit(1);
    }
    syslog(LOG_INFO, "Child: SIGUSR1 handler set for child.");
}
