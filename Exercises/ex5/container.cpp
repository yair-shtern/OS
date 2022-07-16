#include <iostream>
#include <sched.h>
#include <sys/wait.h>
#include <cstring>
#include <sys/unistd.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <fstream>

#define STACK 8192
#define FILE_MODE 0755

char error_msg1[] = "system error: unable to allocate enough memory.";
char error_msg2[] = "system error: Unable to clone new process.";
char error_msg3[] = "system error: Unable to change host name.";
char error_msg4[] = "system error: Unable to mount directory.";
char error_msg5[] = "system error: Unable to create new directory.";
char error_msg6[] = "system error: Unable to open proc file.";
char error_msg7[] = "system error: Unable to change root directory.";

typedef struct {
    char *host_name;
    char *fd;
    char *num_processes;
    char *container_program;
    char **args_for_child;
} arg_struct;

/**
 * Child function that sets and operates the container
 * @param args arguments
 * @return 0
 */
int child(void *args) {
    arg_struct *program_arguments = (arg_struct *) args;

    //changing host name
    int ret = sethostname(program_arguments->host_name, strlen(program_arguments->host_name));
    if (ret == -1) {
        fprintf(stderr, "%s\n", error_msg3);
        exit(1);
    }

    //Setting a new file system
    int chroot_ret = chroot(program_arguments->fd);
    if (chroot_ret == -1) {
        fprintf(stderr, "%s\n", error_msg4);
        exit(1);
    }

    //updating root directory
    int chdir_ret = chdir("/");
    if (chdir_ret == -1) {
        fprintf(stderr, "%s\n", error_msg7);
        exit(1);
    }

    //mounting proc file
    int mount_ret = mount("proc", "/proc", "proc", 0, 0);
    if (mount_ret == -1) {
        fprintf(stderr, "%s\n", error_msg4);
        exit(1);
    }

    //limiting the number of processes
    char fs_path[] = "/sys/fs";
    if (access(fs_path, F_OK)) {
        int fs_ret = mkdir(fs_path, FILE_MODE);
        if (fs_ret == -1) {
            fprintf(stderr, "%s\n", error_msg5);
            exit(1);
        }
    }

    char cgroup_path[] = "/sys/fs/cgroup";
    if (access(cgroup_path, F_OK)) {
        int mkd_ret = mkdir(cgroup_path, FILE_MODE);
        if (mkd_ret == -1) {
            fprintf(stderr, "%s\n", error_msg5);
            exit(1);
        }
    }

    char pid_path[] = "/sys/fs/cgroup/pids";
    if (access(pid_path, F_OK)) {
        int mkd2_ret = mkdir(pid_path, FILE_MODE);
        if (mkd2_ret == -1) {
            fprintf(stderr, "%s\n", error_msg5);
            exit(1);
        }
    }

    //Writing 1 pid into cgroups.procs
    std::ofstream procs_file("/sys/fs/cgroup/pids/cgroup.procs");
    if (procs_file.is_open()) {
        procs_file << 1;
        procs_file.close();
    } else {
        fprintf(stderr, "%s\n", error_msg6);
        exit(1);
    }

    //Writing into the file pids.max num of allowed processes
    std::ofstream pid_file("/sys/fs/cgroup/pids/pids.max");
    if (pid_file.is_open()) {
        pid_file << program_arguments->num_processes;
        pid_file.close();
    } else {
        fprintf(stderr, "%s\n", error_msg6);
        exit(1);
    }

    //running the program inside container
    char *new_exec_args[] = {program_arguments->container_program,
                             *program_arguments->args_for_child, nullptr};
    //notify_on_release
    std::ofstream release_file("/sys/fs/cgroup/pids/notify_on_release");
    if (release_file.is_open()) {
        release_file << 1;
        release_file.close();
    } else {
        fprintf(stderr, "%s\n", error_msg6);
        exit(1);
    }
    execvp(program_arguments->container_program, new_exec_args);
    return 0;
}

/**
 * Main function for initializing a container and
 * removing files and unmounting ones the
 * container is exited
 * @param argc number of input arguments
 * @param argv input arguments
 * @return 0
 */
int main(int argc, char *argv[]) {
    //Setting child's stack
    char *stack = (char *) malloc(STACK);
    if (!stack) {
        fprintf(stderr, "%s\n", error_msg1);
        exit(1);
    }

    // initialize arguments for child
    char *program_args[argc - 5];
        int argv_ind = 5;
        while (argv_ind < argc) {
            program_args[argv_ind - 5] = argv[argv_ind];
            argv_ind++;
        }
    if (argc == 5) {
        *program_args = nullptr;
    }
    arg_struct arguments = {argv[1], argv[2], argv[3],
                            argv[4], program_args};

    // create new child process with specific signal handlers and namespace
    int child_pid = clone(child, stack + STACK, CLONE_NEWUTS | CLONE_NEWPID | CLONE_NEWNS | SIGCHLD, &arguments);
    if (child_pid == -1) {
        fprintf(stderr, "%s\n", error_msg2);
        exit(1);
    }

    // wait for the process to finish
    waitpid(child_pid, nullptr, 0);

    // unmounting
    size_t fd_len = strlen(argv[2]);
    char fd_path[fd_len];
    strcpy(fd_path, argv[2]);
    umount(strcat(fd_path, "/proc"));

    // delete directories
    char remove_cmd[sizeof("rm -rf ")] = "rm -rf ";
    system(strcat(strcat(remove_cmd, argv[2]), "/sys/*"));
    return 0;
}
