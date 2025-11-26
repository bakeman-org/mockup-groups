// #include <dirent.h>
// #include <elf.h>
// #include <errno.h>
// #include <fcntl.h>
// #include <libgen.h>
// #include <limits.h>
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <sys/mman.h>
// #include <sys/stat.h>
// #include <sys/types.h>
// #include <unistd.h>

// // ====================== 原生C链表（无第三方依赖） ======================
// typedef struct DepNode {
//     char *name;           // 依赖库文件名（如 libc.so.6）
//     char *path;           // 依赖库绝对路径
//     struct DepNode *next; // 下一个节点
// } DepNode;

// // 链表操作函数（纯原生C实现，增加内存检查）
// static DepNode *dep_list_create() {
//     return NULL;
// }

// static void dep_list_add(DepNode **head, char const *name, char const *path) {
//     if (!name || !path) {
//         fprintf(stderr, "警告：无效的依赖库名称或路径，跳过添加\n");
//         return;
//     }

//     DepNode *new_node = (DepNode *)malloc(sizeof(DepNode));
//     if (!new_node) {
//         fprintf(stderr, "错误：内存分配失败（DepNode）\n");
//         return;
//     }

//     new_node->name = strdup(name);
//     new_node->path = strdup(path);
//     // 检查strdup是否成功
//     if (!new_node->name || !new_node->path) {
//         fprintf(stderr, "错误：内存分配失败（strdup）\n");
//         free(new_node->name);
//         free(new_node->path);
//         free(new_node);
//         return;
//     }

//     new_node->next = *head;
//     *head = new_node;
// }

// static DepNode *dep_list_find(DepNode *head, char const *name) {
//     if (!name) {
//         return NULL;
//     }
//     DepNode *curr = head;
//     while (curr) {
//         if (strcmp(curr->name, name) == 0) {
//             return curr;
//         }
//         curr = curr->next;
//     }
//     return NULL;
// }

// static int dep_list_count(DepNode *head) {
//     int cnt = 0;
//     DepNode *curr = head;
//     while (curr) {
//         cnt++;
//         curr = curr->next;
//     }
//     return cnt;
// }

// static void dep_list_free(DepNode *head) {
//     DepNode *curr = head;
//     while (curr) {
//         DepNode *tmp = curr;
//         curr = curr->next;
//         free(tmp->name);
//         free(tmp->path);
//         free(tmp);
//     }
// }

// // ====================== 全局配置 ======================
// typedef struct {
//     char **filenames; // 输入ELF文件列表（绝对路径）
//     int filename_cnt; // 输入文件数量
//     char *output;     // 输出目录路径
//     int force;        // 强制覆盖（-f）
//     int dry;          // 干跑模式（-D）
//     int patch;        // 启用ELF修改（-P）
//     char *suffix;     // 启动脚本后缀（-x，默认.sh）
// } Args;

// // 全局变量
// static Args g_args = {0};
// static DepNode *g_dep_list = NULL; // 依赖库链表
// static char *g_ld_linux =
//     NULL; // 动态链接器路径（如 /lib64/ld-linux-x86-64.so.2）

// // ====================== 基础工具函数（纯原生C） ======================
// /**
//  * 检查文件是否存在且可读
//  */
// static int file_exists(char const *path) {
//     if (!path) {
//         return 0;
//     }
//     return access(path, R_OK) == 0;
// }

// /**
//  * 获取文件绝对路径（失败返回NULL，需调用者free）
//  */
// static char *get_absolute_path(char const *path) {
//     if (!path) {
//         return NULL;
//     }
//     char abs_path[PATH_MAX];
//     if (realpath(path, abs_path) == NULL) {
//         fprintf(stderr, "错误：无法获取绝对路径 '%s'（%s）\n", path,
//                 strerror(errno));
//         return NULL;
//     }
//     return strdup(abs_path);
// }

// /**
//  * 安全获取文件名（basename包装，避免free静态缓冲区）
//  * 返回值：堆内存字符串，需调用者free
//  */
// static char *safe_basename(char const *path) {
//     if (!path) {
//         return NULL;
//     }
//     // basename可能返回静态缓冲区，不能直接free，需strdup复制
//     char *base = basename((char *)path);
//     return base ? strdup(base) : NULL;
// }

// /**
//  * 递归创建目录（纯原生C实现）
//  */
// static int mkdir_recursive(char const *dir) {
//     if (!dir) {
//         return -1;
//     }
//     char tmp[PATH_MAX];
//     char *p = NULL;
//     size_t len;

//     snprintf(tmp, sizeof(tmp), "%s", dir);
//     len = strlen(tmp);
//     if (len == 0) {
//         return -1;
//     }
//     if (tmp[len - 1] == '/') {
//         tmp[len - 1] = '\0';
//     }

//     for (p = tmp + 1; *p; p++) {
//         if (*p == '/') {
//             *p = '\0';
//             if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
//                 fprintf(stderr, "错误：创建目录 '%s' 失败（%s）\n", tmp,
//                         strerror(errno));
//                 return -1;
//             }
//             *p = '/';
//         }
//     }

//     if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
//         fprintf(stderr, "错误：创建目录 '%s' 失败（%s）\n", tmp,
//                 strerror(errno));
//         return -1;
//     }
//     return 0;
// }

// /**
//  * 递归删除目录（纯原生C实现）
//  */
// static int rmdir_recursive(char const *dir) {
//     if (!dir) {
//         return -1;
//     }
//     char path[PATH_MAX];
//     struct dirent *dp;
//     DIR *dirp = opendir(dir);

//     if (!dirp) {
//         fprintf(stderr, "错误：打开目录 '%s' 失败（%s）\n", dir,
//                 strerror(errno));
//         return -1;
//     }

//     while ((dp = readdir(dirp)) != NULL) {
//         if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0) {
//             continue;
//         }

//         snprintf(path, sizeof(path), "%s/%s", dir, dp->d_name);
//         struct stat st;
//         if (stat(path, &st) == -1) {
//             fprintf(stderr, "警告：获取文件状态 '%s' 失败（%s）\n", path,
//                     strerror(errno));
//             continue;
//         }

//         if (S_ISDIR(st.st_mode)) {
//             if (rmdir_recursive(path) != 0) {
//                 closedir(dirp);
//                 return -1;
//             }
//         } else {
//             if (unlink(path) != 0) {
//                 fprintf(stderr, "错误：删除文件 '%s' 失败（%s）\n", path,
//                         strerror(errno));
//                 closedir(dirp);
//                 return -1;
//             }
//         }
//     }

//     closedir(dirp);
//     if (rmdir(dir) != 0) {
//         fprintf(stderr, "错误：删除目录 '%s' 失败（%s）\n", dir,
//                 strerror(errno));
//         return -1;
//     }
//     return 0;
// }

// /**
//  * 复制文件（纯原生C实现，无第三方依赖）
//  */
// static int copy_file(char const *src, char const *dest) {
//     if (!src || !dest) {
//         fprintf(stderr, "错误：源文件或目标文件路径为空\n");
//         return -1;
//     }

//     int src_fd = open(src, O_RDONLY);
//     if (src_fd == -1) {
//         fprintf(stderr, "错误：打开源文件 '%s' 失败（%s）\n", src,
//                 strerror(errno));
//         return -1;
//     }

//     int dest_fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0755); // 可执行权限
//     if (dest_fd == -1) {
//         fprintf(stderr, "错误：创建目标文件 '%s' 失败（%s）\n", dest,
//                 strerror(errno));
//         close(src_fd);
//         return -1;
//     }

//     char buf[4096];
//     ssize_t n;
//     while ((n = read(src_fd, buf, sizeof(buf))) > 0) {
//         if (write(dest_fd, buf, n) != n) {
//             fprintf(stderr, "错误：复制文件 '%s'->'%s' 失败（%s）\n", src, dest,
//                     strerror(errno));
//             close(src_fd);
//             close(dest_fd);
//             unlink(dest); // 删除不完整的目标文件
//             return -1;
//         }
//     }

//     if (n == -1) {
//         fprintf(stderr, "错误：读取源文件 '%s' 失败（%s）\n", src,
//                 strerror(errno));
//         close(src_fd);
//         close(dest_fd);
//         unlink(dest);
//         return -1;
//     }

//     // 复制文件权限（简化：直接设置为0755，确保可执行）
//     fchmod(dest_fd, 0755);

//     close(src_fd);
//     close(dest_fd);
//     return 0;
// }

// // ====================== ELF解析工具函数（纯原生C） ======================
// /**
//  * 内存映射ELF文件（高效读取）
//  * 返回：映射地址，失败返回NULL；size和fd通过参数传出
//  */
// static void *elf_mmap(char const *path, size_t *size, int *fd) {
//     if (!path || !size || !fd) {
//         return NULL;
//     }
//     *fd = open(path, O_RDONLY);
//     if (*fd == -1) {
//         fprintf(stderr, "错误：打开ELF文件 '%s' 失败（%s）\n", path,
//                 strerror(errno));
//         return NULL;
//     }

//     struct stat st;
//     if (fstat(*fd, &st) == -1) {
//         fprintf(stderr, "错误：获取文件状态 '%s' 失败（%s）\n", path,
//                 strerror(errno));
//         close(*fd);
//         return NULL;
//     }
//     *size = st.st_size;

//     void *addr = mmap(NULL, *size, PROT_READ, MAP_PRIVATE, *fd, 0);
//     if (addr == MAP_FAILED) {
//         fprintf(stderr, "错误：映射文件 '%s' 失败（%s）\n", path,
//                 strerror(errno));
//         close(*fd);
//         return NULL;
//     }

//     // 验证ELF标识
//     if (memcmp(addr, ELFMAG, SELFMAG) != 0) {
//         fprintf(stderr, "错误：'%s' 不是ELF文件\n", path);
//         munmap(addr, *size);
//         close(*fd);
//         return NULL;
//     }

//     return addr;
// }

// /**
//  * 解除ELF文件内存映射
//  */
// static void elf_unmap(void *addr, size_t size, int fd) {
//     if (addr != MAP_FAILED && addr != NULL) {
//         munmap(addr, size);
//     }
//     if (fd >= 0) {
//         close(fd);
//     }
// }

// /**
//  * 获取ELF文件的动态链接器路径（PT_INTERP段）
//  * 返回：动态链接器路径（堆内存），失败返回NULL，需调用者free
//  */
// static char *elf_get_interpreter(char const *path) {
//     if (!path) {
//         return NULL;
//     }
//     size_t size;
//     int fd;
//     void *addr = elf_mmap(path, &size, &fd);
//     if (!addr) {
//         return NULL;
//     }

//     Elf64_Ehdr *ehdr64 = (Elf64_Ehdr *)addr;
//     char *interp = NULL;

//     switch (ehdr64->e_ident[EI_CLASS]) {
//     case ELFCLASS64: {
//         Elf64_Phdr *phdr = (Elf64_Phdr *)(addr + ehdr64->e_phoff);
//         for (int i = 0; i < ehdr64->e_phnum; i++) {
//             if (phdr[i].p_type == PT_INTERP) {
//                 interp = strdup((char *)(addr + phdr[i].p_offset));
//                 break;
//             }
//         }
//         break;
//     }
//     case ELFCLASS32: {
//         Elf32_Ehdr *ehdr32 = (Elf32_Ehdr *)addr;
//         Elf32_Phdr *phdr = (Elf32_Phdr *)(addr + ehdr32->e_phoff);
//         for (int i = 0; i < ehdr32->e_phnum; i++) {
//             if (phdr[i].p_type == PT_INTERP) {
//                 interp = strdup((char *)(addr + phdr[i].p_offset));
//                 break;
//             }
//         }
//         break;
//     }
//     default:
//         fprintf(stderr, "错误：不支持的ELF架构（%d）\n",
//                 ehdr64->e_ident[EI_CLASS]);
//     }

//     elf_unmap(addr, size, fd);
//     return interp;
// }

// /**
//  * 获取ELF文件的依赖库列表（DT_NEEDED）
//  * @return 依赖库名称数组（NULL终止），需调用者释放（free每个元素+数组本身）
//  */
// static char **elf_get_needed(char const *path, int *count) {
//     if (!path || !count) {
//         return NULL;
//     }
//     *count = 0;
//     size_t size;
//     int fd;
//     void *addr = elf_mmap(path, &size, &fd);
//     if (!addr) {
//         return NULL;
//     }

//     Elf64_Ehdr *ehdr64 = (Elf64_Ehdr *)addr;
//     char **needed = NULL;
//     char const *dynstr = NULL;

//     switch (ehdr64->e_ident[EI_CLASS]) {
//     case ELFCLASS64: {
//         Elf64_Shdr *shdr = (Elf64_Shdr *)(addr + ehdr64->e_shoff);
//         Elf64_Shdr *dyn_shdr = NULL;
//         Elf64_Shdr *dynstr_shdr = NULL;

//         // 查找.dynamic和.dynstr段
//         for (int i = 0; i < ehdr64->e_shnum; i++) {
//             if (shdr[i].sh_type == SHT_DYNAMIC) {
//                 dyn_shdr = &shdr[i];
//             } else if (shdr[i].sh_type == SHT_STRTAB) {
//                 // 查找段名（.dynstr）
//                 char *sh_name =
//                     (char *)(addr + shdr[ehdr64->e_shstrndx].sh_offset +
//                              shdr[i].sh_name);
//                 if (sh_name && strcmp(sh_name, ".dynstr") == 0) {
//                     dynstr_shdr = &shdr[i];
//                     dynstr = (char *)(addr + dynstr_shdr->sh_offset);
//                 }
//             }
//         }

//         if (!dyn_shdr || !dynstr_shdr) {
//             fprintf(stderr, "警告：'%s' 无动态段（静态链接）\n", path);
//             break;
//         }

//         // 遍历DT_NEEDED条目
//         Elf64_Dyn *dyn = (Elf64_Dyn *)(addr + dyn_shdr->sh_offset);
//         for (; dyn->d_tag != DT_NULL; dyn++) {
//             if (dyn->d_tag == DT_NEEDED) {
//                 char const *lib_name = dynstr + dyn->d_un.d_val;
//                 if (!lib_name) {
//                     continue;
//                 }
//                 needed =
//                     (char **)realloc(needed, sizeof(char *) * (*count + 1));
//                 if (!needed) {
//                     fprintf(stderr, "错误：内存分配失败（needed数组）\n");
//                     break;
//                 }
//                 needed[*count] = strdup(lib_name);
//                 (*count)++;
//             }
//         }
//         break;
//     }
//     case ELFCLASS32: {
//         Elf32_Ehdr *ehdr32 = (Elf32_Ehdr *)addr;
//         Elf32_Shdr *shdr = (Elf32_Shdr *)(addr + ehdr32->e_shoff);
//         Elf32_Shdr *dyn_shdr = NULL;
//         Elf32_Shdr *dynstr_shdr = NULL;

//         for (int i = 0; i < ehdr32->e_shnum; i++) {
//             if (shdr[i].sh_type == SHT_DYNAMIC) {
//                 dyn_shdr = &shdr[i];
//             } else if (shdr[i].sh_type == SHT_STRTAB) {
//                 char *sh_name =
//                     (char *)(addr + shdr[ehdr32->e_shstrndx].sh_offset +
//                              shdr[i].sh_name);
//                 if (sh_name && strcmp(sh_name, ".dynstr") == 0) {
//                     dynstr_shdr = &shdr[i];
//                     dynstr = (char *)(addr + dynstr_shdr->sh_offset);
//                 }
//             }
//         }

//         if (!dyn_shdr || !dynstr_shdr) {
//             fprintf(stderr, "警告：'%s' 无动态段（静态链接）\n", path);
//             break;
//         }

//         Elf32_Dyn *dyn = (Elf32_Dyn *)(addr + dyn_shdr->sh_offset);
//         for (; dyn->d_tag != DT_NULL; dyn++) {
//             if (dyn->d_tag == DT_NEEDED) {
//                 char const *lib_name = dynstr + dyn->d_un.d_val;
//                 if (!lib_name) {
//                     continue;
//                 }
//                 needed =
//                     (char **)realloc(needed, sizeof(char *) * (*count + 1));
//                 if (!needed) {
//                     fprintf(stderr, "错误：内存分配失败（needed数组）\n");
//                     break;
//                 }
//                 needed[*count] = strdup(lib_name);
//                 (*count)++;
//             }
//         }
//         break;
//     }
//     default:
//         fprintf(stderr, "错误：不支持的ELF架构（%d）\n",
//                 ehdr64->e_ident[EI_CLASS]);
//     }

//     // 添加NULL终止符
//     needed = (char **)realloc(needed, sizeof(char *) * (*count + 1));
//     if (needed) {
//         needed[*count] = NULL;
//     }

//     elf_unmap(addr, size, fd);
//     return needed;
// }

// /**
//  * 查找依赖库的绝对路径（模拟动态链接器规则）
//  * 返回：绝对路径（堆内存），失败返回NULL，需调用者free
//  */
// static char *elf_find_library(char const *lib_name) {
//     if (!lib_name || strlen(lib_name) == 0) {
//         return NULL;
//     }

//     // 标准库路径（Linux通用路径）
//     char const *std_paths[] = {"/lib",
//                                "/usr/lib",
//                                "/lib64",
//                                "/usr/lib64",
//                                "/usr/local/lib",
//                                "/usr/local/lib64",
//                                "/lib/x86_64-linux-gnu",
//                                "/usr/lib/x86_64-linux-gnu", // Debian/Ubuntu
//                                "/lib/i386-linux-gnu",
//                                "/usr/lib/i386-linux-gnu",
//                                "/lib/aarch64-linux-gnu",
//                                "/usr/lib/aarch64-linux-gnu", // ARM64
//                                NULL};

//     // 1. 检查LD_LIBRARY_PATH环境变量
//     char *ld_lib_path = getenv("LD_LIBRARY_PATH");
//     if (ld_lib_path) {
//         char *path_copy = strdup(ld_lib_path);
//         if (path_copy) {
//             char *dir = strtok(path_copy, ":");
//             while (dir) {
//                 char lib_path[PATH_MAX];
//                 snprintf(lib_path, sizeof(lib_path), "%s/%s", dir, lib_name);
//                 if (file_exists(lib_path)) {
//                     char *abs_path = get_absolute_path(lib_path);
//                     free(path_copy);
//                     return abs_path;
//                 }
//                 dir = strtok(NULL, ":");
//             }
//             free(path_copy);
//         }
//     }

//     // 2. 检查标准库路径
//     for (int i = 0; std_paths[i]; i++) {
//         char lib_path[PATH_MAX];
//         snprintf(lib_path, sizeof(lib_path), "%s/%s", std_paths[i], lib_name);
//         if (file_exists(lib_path)) {
//             return get_absolute_path(lib_path);
//         }
//     }

//     fprintf(stderr, "警告：未找到依赖库 '%s'\n", lib_name);
//     return NULL;
// }

// // ====================== ELF修改工具函数（纯原生C，修复指针类型）
// // ======================
// /**
//  * 修改ELF文件的动态链接器（PT_INTERP段）
//  */
// static int elf_set_interpreter(char const *path, char const *new_interp) {
//     if (!path || !new_interp) {
//         fprintf(stderr, "错误：路径或新动态链接器为空\n");
//         return -1;
//     }

//     int fd = open(path, O_RDWR);
//     if (fd == -1) {
//         fprintf(stderr, "错误：打开ELF文件 '%s' 失败（%s）\n", path,
//                 strerror(errno));
//         return -1;
//     }

//     struct stat st;
//     if (fstat(fd, &st) == -1) {
//         fprintf(stderr, "错误：获取文件状态 '%s' 失败（%s）\n", path,
//                 strerror(errno));
//         close(fd);
//         return -1;
//     }

//     void *addr =
//         mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
//     if (addr == MAP_FAILED) {
//         fprintf(stderr, "错误：映射文件 '%s' 失败（%s）\n", path,
//                 strerror(errno));
//         close(fd);
//         return -1;
//     }

//     Elf64_Ehdr *ehdr64 = (Elf64_Ehdr *)addr;
//     int ret = -1;

//     switch (ehdr64->e_ident[EI_CLASS]) {
//     case ELFCLASS64: {
//         Elf64_Phdr *phdr = (Elf64_Phdr *)(addr + ehdr64->e_phoff);
//         for (int i = 0; i < ehdr64->e_phnum; i++) {
//             if (phdr[i].p_type == PT_INTERP) {
//                 // 检查新路径长度是否超出原有空间
//                 if (strlen(new_interp) >= phdr[i].p_filesz) {
//                     fprintf(stderr,
//                             "错误：动态链接器路径过长（最大支持 %zu 字节）\n",
//                             phdr[i].p_filesz - 1);
//                     goto out;
//                 }
//                 // 覆盖写入新路径
//                 memset((char *)(addr + phdr[i].p_offset), 0, phdr[i].p_filesz);
//                 strncpy((char *)(addr + phdr[i].p_offset), new_interp,
//                         phdr[i].p_filesz - 1);
//                 ret = 0;
//                 break;
//             }
//         }
//         break;
//     }
//     case ELFCLASS32: {
//         Elf32_Ehdr *ehdr32 = (Elf32_Ehdr *)addr;
//         Elf32_Phdr *phdr = (Elf32_Phdr *)(addr + ehdr32->e_phoff);
//         for (int i = 0; i < ehdr32->e_phnum; i++) {
//             if (phdr[i].p_type == PT_INTERP) {
//                 if (strlen(new_interp) >= phdr[i].p_filesz) {
//                     fprintf(stderr,
//                             "错误：动态链接器路径过长（最大支持 %zu 字节）\n",
//                             phdr[i].p_filesz - 1);
//                     goto out;
//                 }
//                 memset((char *)(addr + phdr[i].p_offset), 0, phdr[i].p_filesz);
//                 strncpy((char *)(addr + phdr[i].p_offset), new_interp,
//                         phdr[i].p_filesz - 1);
//                 ret = 0;
//                 break;
//             }
//         }
//         break;
//     }
//     default:
//         fprintf(stderr, "错误：不支持的ELF架构（%d）\n",
//                 ehdr64->e_ident[EI_CLASS]);
//     }

// out:
//     msync(addr, st.st_size, MS_SYNC); // 同步修改到磁盘
//     munmap(addr, st.st_size);
//     close(fd);
//     return ret;
// }

// /**
//  * 设置ELF文件的RPATH（DT_RPATH/DT_RUNPATH）- 修复指针类型混用
//  */
// static int elf_set_rpath(char const *path, char const *rpath) {
//     if (!path || !rpath) {
//         fprintf(stderr, "错误：路径或RPATH为空\n");
//         return -1;
//     }

//     int fd = open(path, O_RDWR);
//     if (fd == -1) {
//         fprintf(stderr, "错误：打开ELF文件 '%s' 失败（%s）\n", path,
//                 strerror(errno));
//         return -1;
//     }

//     struct stat st;
//     if (fstat(fd, &st) == -1) {
//         fprintf(stderr, "错误：获取文件状态 '%s' 失败（%s）\n", path,
//                 strerror(errno));
//         close(fd);
//         return -1;
//     }

//     void *addr =
//         mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
//     if (addr == MAP_FAILED) {
//         fprintf(stderr, "错误：映射文件 '%s' 失败（%s）\n", path,
//                 strerror(errno));
//         close(fd);
//         return -1;
//     }

//     Elf64_Ehdr *ehdr64 = (Elf64_Ehdr *)addr;
//     int ret = -1;

//     switch (ehdr64->e_ident[EI_CLASS]) {
//     case ELFCLASS64: {
//         Elf64_Shdr *shdr = (Elf64_Shdr *)(addr + ehdr64->e_shoff);
//         Elf64_Shdr *dyn_shdr = NULL;    // 64位专用指针
//         Elf64_Shdr *dynstr_shdr = NULL; // 64位专用指针
//         char const *dynstr = NULL;

//         // 查找.dynamic和.dynstr段
//         for (int i = 0; i < ehdr64->e_shnum; i++) {
//             if (shdr[i].sh_type == SHT_DYNAMIC) {
//                 dyn_shdr = &shdr[i];
//             } else if (shdr[i].sh_type == SHT_STRTAB) {
//                 char *sh_name =
//                     (char *)(addr + shdr[ehdr64->e_shstrndx].sh_offset +
//                              shdr[i].sh_name);
//                 if (sh_name && strcmp(sh_name, ".dynstr") == 0) {
//                     dynstr_shdr = &shdr[i];
//                     dynstr = (char *)(addr + dynstr_shdr->sh_offset);
//                 }
//             }
//         }

//         if (!dyn_shdr || !dynstr_shdr) {
//             fprintf(stderr, "错误：'%s' 无动态段\n", path);
//             goto out;
//         }

//         // 查找DT_RPATH/DT_RUNPATH条目并修改
//         Elf64_Dyn *dyn = (Elf64_Dyn *)(addr + dyn_shdr->sh_offset);
//         for (; dyn->d_tag != DT_NULL; dyn++) {
//             if (dyn->d_tag == DT_RPATH || dyn->d_tag == DT_RUNPATH) {
//                 size_t rpath_len = strlen(rpath);
//                 size_t strtab_size = dynstr_shdr->sh_size;
//                 size_t str_offset = dyn->d_un.d_val;

//                 if (str_offset + rpath_len + 1 > strtab_size) {
//                     fprintf(stderr, "错误：字符串表空间不足，无法设置RPATH\n");
//                     goto out;
//                 }

//                 // 写入新RPATH
//                 memset((char *)(addr + dynstr_shdr->sh_offset + str_offset), 0,
//                        rpath_len + 1);
//                 strncpy((char *)(addr + dynstr_shdr->sh_offset + str_offset),
//                         rpath, rpath_len);
//                 ret = 0;
//                 break;
//             }
//         }

//         if (dyn->d_tag == DT_NULL) {
//             fprintf(stderr,
//                     "警告：'%s' 无DT_RPATH/DT_RUNPATH条目，跳过RPATH设置\n",
//                     path);
//             ret = 0; // 非致命错误
//         }
//         break;
//     }
//     case ELFCLASS32: {
//         Elf32_Ehdr *ehdr32 = (Elf32_Ehdr *)addr;
//         Elf32_Shdr *shdr = (Elf32_Shdr *)(addr + ehdr32->e_shoff);
//         Elf32_Shdr *dyn_shdr = NULL;    // 32位专用指针（修复类型混用）
//         Elf32_Shdr *dynstr_shdr = NULL; // 32位专用指针（修复类型混用）
//         char const *dynstr = NULL;

//         // 查找.dynamic和.dynstr段
//         for (int i = 0; i < ehdr32->e_shnum; i++) {
//             if (shdr[i].sh_type == SHT_DYNAMIC) {
//                 dyn_shdr = &shdr[i]; // 现在是同类型赋值，无警告
//             } else if (shdr[i].sh_type == SHT_STRTAB) {
//                 char *sh_name =
//                     (char *)(addr + shdr[ehdr32->e_shstrndx].sh_offset +
//                              shdr[i].sh_name);
//                 if (sh_name && strcmp(sh_name, ".dynstr") == 0) {
//                     dynstr_shdr = &shdr[i]; // 同类型赋值，无警告
//                     dynstr = (char *)(addr + dynstr_shdr->sh_offset);
//                 }
//             }
//         }

//         if (!dyn_shdr || !dynstr_shdr) {
//             fprintf(stderr, "错误：'%s' 无动态段\n", path);
//             goto out;
//         }

//         // 查找DT_RPATH/DT_RUNPATH条目并修改
//         Elf32_Dyn *dyn = (Elf32_Dyn *)(addr + dyn_shdr->sh_offset);
//         for (; dyn->d_tag != DT_NULL; dyn++) {
//             if (dyn->d_tag == DT_RPATH || dyn->d_tag == DT_RUNPATH) {
//                 size_t rpath_len = strlen(rpath);
//                 size_t strtab_size = dynstr_shdr->sh_size;
//                 size_t str_offset = dyn->d_un.d_val;

//                 if (str_offset + rpath_len + 1 > strtab_size) {
//                     fprintf(stderr, "错误：字符串表空间不足，无法设置RPATH\n");
//                     goto out;
//                 }

//                 memset((char *)(addr + dynstr_shdr->sh_offset + str_offset), 0,
//                        rpath_len + 1);
//                 strncpy((char *)(addr + dynstr_shdr->sh_offset + str_offset),
//                         rpath, rpath_len);
//                 ret = 0;
//                 break;
//             }
//         }

//         if (dyn->d_tag == DT_NULL) {
//             fprintf(stderr,
//                     "警告：'%s' 无DT_RPATH/DT_RUNPATH条目，跳过RPATH设置\n",
//                     path);
//             ret = 0;
//         }
//         break;
//     }
//     default:
//         fprintf(stderr, "错误：不支持的ELF架构（%d）\n",
//                 ehdr64->e_ident[EI_CLASS]);
//     }

// out:
//     msync(addr, st.st_size, MS_SYNC);
//     munmap(addr, st.st_size);
//     close(fd);
//     return ret;
// }

// /**
//  * 设置ELF文件的SONAME（DT_SONAME）- 修复指针类型混用
//  */
// static int elf_set_soname(char const *path, char const *soname) {
//     if (!path || !soname) {
//         fprintf(stderr, "错误：路径或SONAME为空\n");
//         return -1;
//     }

//     int fd = open(path, O_RDWR);
//     if (fd == -1) {
//         fprintf(stderr, "错误：打开ELF文件 '%s' 失败（%s）\n", path,
//                 strerror(errno));
//         return -1;
//     }

//     struct stat st;
//     if (fstat(fd, &st) == -1) {
//         fprintf(stderr, "错误：获取文件状态 '%s' 失败（%s）\n", path,
//                 strerror(errno));
//         close(fd);
//         return -1;
//     }

//     void *addr =
//         mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
//     if (addr == MAP_FAILED) {
//         fprintf(stderr, "错误：映射文件 '%s' 失败（%s）\n", path,
//                 strerror(errno));
//         close(fd);
//         return -1;
//     }

//     Elf64_Ehdr *ehdr64 = (Elf64_Ehdr *)addr;
//     int ret = -1;

//     switch (ehdr64->e_ident[EI_CLASS]) {
//     case ELFCLASS64: {
//         Elf64_Shdr *shdr = (Elf64_Shdr *)(addr + ehdr64->e_shoff);
//         Elf64_Shdr *dyn_shdr = NULL;
//         Elf64_Shdr *dynstr_shdr = NULL;
//         char const *dynstr = NULL;

//         for (int i = 0; i < ehdr64->e_shnum; i++) {
//             if (shdr[i].sh_type == SHT_DYNAMIC) {
//                 dyn_shdr = &shdr[i];
//             } else if (shdr[i].sh_type == SHT_STRTAB) {
//                 char *sh_name =
//                     (char *)(addr + shdr[ehdr64->e_shstrndx].sh_offset +
//                              shdr[i].sh_name);
//                 if (sh_name && strcmp(sh_name, ".dynstr") == 0) {
//                     dynstr_shdr = &shdr[i];
//                     dynstr = (char *)(addr + dynstr_shdr->sh_offset);
//                 }
//             }
//         }

//         if (!dyn_shdr || !dynstr_shdr) {
//             fprintf(stderr, "错误：'%s' 无动态段\n", path);
//             goto out;
//         }

//         // 查找DT_SONAME条目并修改
//         Elf64_Dyn *dyn = (Elf64_Dyn *)(addr + dyn_shdr->sh_offset);
//         for (; dyn->d_tag != DT_NULL; dyn++) {
//             if (dyn->d_tag == DT_SONAME) {
//                 size_t soname_len = strlen(soname);
//                 size_t strtab_size = dynstr_shdr->sh_size;
//                 size_t str_offset = dyn->d_un.d_val;

//                 if (str_offset + soname_len + 1 > strtab_size) {
//                     fprintf(stderr, "错误：字符串表空间不足，无法设置SONAME\n");
//                     goto out;
//                 }

//                 memset((char *)(addr + dynstr_shdr->sh_offset + str_offset), 0,
//                        soname_len + 1);
//                 strncpy((char *)(addr + dynstr_shdr->sh_offset + str_offset),
//                         soname, soname_len);
//                 ret = 0;
//                 break;
//             }
//         }

//         if (dyn->d_tag == DT_NULL) {
//             fprintf(stderr, "警告：'%s' 无DT_SONAME条目，跳过SONAME设置\n",
//                     path);
//             ret = 0;
//         }
//         break;
//     }
//     case ELFCLASS32: {
//         Elf32_Ehdr *ehdr32 = (Elf32_Ehdr *)addr;
//         Elf32_Shdr *shdr = (Elf32_Shdr *)(addr + ehdr32->e_shoff);
//         Elf32_Shdr *dyn_shdr = NULL;    // 32位专用指针（修复类型混用）
//         Elf32_Shdr *dynstr_shdr = NULL; // 32位专用指针（修复类型混用）
//         char const *dynstr = NULL;

//         for (int i = 0; i < ehdr32->e_shnum; i++) {
//             if (shdr[i].sh_type == SHT_DYNAMIC) {
//                 dyn_shdr = &shdr[i]; // 同类型赋值，无警告
//             } else if (shdr[i].sh_type == SHT_STRTAB) {
//                 char *sh_name =
//                     (char *)(addr + shdr[ehdr32->e_shstrndx].sh_offset +
//                              shdr[i].sh_name);
//                 if (sh_name && strcmp(sh_name, ".dynstr") == 0) {
//                     dynstr_shdr = &shdr[i]; // 同类型赋值，无警告
//                     dynstr = (char *)(addr + dynstr_shdr->sh_offset);
//                 }
//             }
//         }

//         if (!dyn_shdr || !dynstr_shdr) {
//             fprintf(stderr, "错误：'%s' 无动态段\n", path);
//             goto out;
//         }

//         // 查找DT_SONAME条目并修改
//         Elf32_Dyn *dyn = (Elf32_Dyn *)(addr + dyn_shdr->sh_offset);
//         for (; dyn->d_tag != DT_NULL; dyn++) {
//             if (dyn->d_tag == DT_SONAME) {
//                 size_t soname_len = strlen(soname);
//                 size_t strtab_size = dynstr_shdr->sh_size;
//                 size_t str_offset = dyn->d_un.d_val;

//                 if (str_offset + soname_len + 1 > strtab_size) {
//                     fprintf(stderr, "错误：字符串表空间不足，无法设置SONAME\n");
//                     goto out;
//                 }

//                 memset((char *)(addr + dynstr_shdr->sh_offset + str_offset), 0,
//                        soname_len + 1);
//                 strncpy((char *)(addr + dynstr_shdr->sh_offset + str_offset),
//                         soname, soname_len);
//                 ret = 0;
//                 break;
//             }
//         }

//         if (dyn->d_tag == DT_NULL) {
//             fprintf(stderr, "警告：'%s' 无DT_SONAME条目，跳过SONAME设置\n",
//                     path);
//             ret = 0;
//         }
//         break;
//     }
//     default:
//         fprintf(stderr, "错误：不支持的ELF架构（%d）\n",
//                 ehdr64->e_ident[EI_CLASS]);
//     }

// out:
//     msync(addr, st.st_size, MS_SYNC);
//     munmap(addr, st.st_size);
//     close(fd);
//     return ret;
// }

// // ====================== 依赖查找核心函数 ======================
// /**
//  * 递归查找ELF文件的所有依赖库
//  */
// static int find_dependencies_recursive(char const *path) {
//     if (!path) {
//         return -1;
//     }

//     // 安全获取文件名（避免free静态缓冲区）
//     char *lib_name = safe_basename(path);
//     if (!lib_name) {
//         fprintf(stderr, "警告：无法获取文件名 '%s'\n", path);
//         return -1;
//     }

//     // 检查是否已存在于依赖列表
//     if (dep_list_find(g_dep_list, lib_name)) {
//         free(lib_name); // 释放safe_basename分配的堆内存
//         return 0;
//     }

//     // 获取依赖库列表
//     int needed_cnt;
//     char **needed = elf_get_needed(path, &needed_cnt);
//     if (!needed) {
//         free(lib_name);
//         return -1;
//     }

//     // 添加当前文件到依赖列表
//     dep_list_add(&g_dep_list, lib_name, path);
//     free(lib_name); // 此处必须free（safe_basename的返回值）

//     // 递归查找每个依赖
//     for (int i = 0; i < needed_cnt; i++) {
//         char *dep_name = needed[i];
//         if (!dep_name) {
//             continue;
//         }

//         char *dep_path = elf_find_library(dep_name);
//         if (dep_path) {
//             fprintf(stdout, "找到依赖：%s → %s\n", dep_name, dep_path);
//             find_dependencies_recursive(dep_path); // 递归查找子依赖
//             free(dep_path);
//         }
//         free(dep_name); // 释放elf_get_needed分配的堆内存
//     }

//     free(needed); // 释放依赖数组
//     return 0;
// }

// /**
//  * 初始化依赖查找（入口函数）
//  */
// static int find_all_dependencies() {
//     // 初始化依赖列表
//     g_dep_list = dep_list_create();

//     // 处理每个输入文件
//     for (int i = 0; i < g_args.filename_cnt; i++) {
//         char *path = g_args.filenames[i];
//         if (!path) {
//             continue;
//         }

//         fprintf(stdout, "正在解析：%s\n", path);

//         // 验证文件是否存在
//         if (!file_exists(path)) {
//             fprintf(stderr, "错误：文件 '%s' 不存在或不可读\n", path);
//             return -1;
//         }

//         // 获取动态链接器（仅第一次获取）
//         if (!g_ld_linux) {
//             g_ld_linux = elf_get_interpreter(path);
//             if (!g_ld_linux) {
//                 fprintf(stderr,
//                         "错误：无法获取动态链接器路径（可能是静态链接文件）\n");
//                 return -1;
//             }
//             fprintf(stdout, "找到动态链接器：%s\n", g_ld_linux);
//         }

//         // 递归查找依赖
//         find_dependencies_recursive(path);
//     }

//     fprintf(stdout, "依赖解析完成：共 %d 个依赖库 + 1 个动态链接器\n",
//             dep_list_count(g_dep_list));
//     return 0;
// }

// // ====================== 打包核心函数 ======================
// /**
//  * 复制依赖文件到输出目录
//  */
// static int copy_dependencies() {
//     if (!g_ld_linux) {
//         fprintf(stderr, "错误：未找到动态链接器\n");
//         return -1;
//     }

//     char dest_path[PATH_MAX];
//     char *ld_linux_name = safe_basename(g_ld_linux);
//     if (!ld_linux_name) {
//         fprintf(stderr, "错误：无法获取动态链接器文件名\n");
//         return -1;
//     }

//     // 复制动态链接器
//     snprintf(dest_path, sizeof(dest_path), "%s/%s", g_args.output,
//              ld_linux_name);
//     fprintf(stdout, "复制动态链接器：%s → %s\n", g_ld_linux, dest_path);
//     if (!g_args.dry) {
//         if (copy_file(g_ld_linux, dest_path) != 0) {
//             free(ld_linux_name);
//             return -1;
//         }
//     }

//     // 复制依赖库
//     DepNode *curr = g_dep_list;
//     while (curr) {
//         snprintf(dest_path, sizeof(dest_path), "%s/%s", g_args.output,
//                  curr->name);
//         fprintf(stdout, "复制依赖库：%s → %s\n", curr->path, dest_path);
//         if (!g_args.dry) {
//             copy_file(curr->path, dest_path);
//         }
//         curr = curr->next;
//     }

//     free(ld_linux_name);
//     return 0;
// }

// /**
//  * 修改ELF文件（RPATH/SONAME/INTERP）- 已修复字符串拼接
//  */
// static int patch_elf_files() {
//     if (!g_ld_linux) {
//         fprintf(stderr, "错误：未找到动态链接器\n");
//         return -1;
//     }

//     char dest_path[PATH_MAX];
//     char *ld_linux_name = safe_basename(g_ld_linux);
//     char interp_path[PATH_MAX]; // 动态链接器路径缓冲区
//     if (!ld_linux_name) {
//         fprintf(stderr, "错误：无法获取动态链接器文件名\n");
//         return -1;
//     }

//     // 拼接 "./动态链接器名"（正确的字符串拼接方式）
//     snprintf(interp_path, sizeof(interp_path), "./%s", ld_linux_name);

//     DepNode *curr = g_dep_list;
//     while (curr) {
//         snprintf(dest_path, sizeof(dest_path), "%s/%s", g_args.output,
//                  curr->name);
//         fprintf(stdout, "修改ELF文件：%s\n", dest_path);

//         if (!g_args.dry) {
//             // 设置RPATH为当前目录（$ORIGIN表示ELF文件所在目录）
//             elf_set_rpath(dest_path, "$ORIGIN");

//             // 设置SONAME（仅SO库）
//             if (strstr(curr->name, ".so") != NULL) {
//                 elf_set_soname(dest_path, curr->name);
//             }

//             // 检查是否为输入文件，设置动态链接器
//             int is_input = 0;
//             for (int i = 0; i < g_args.filename_cnt; i++) {
//                 if (strcmp(curr->path, g_args.filenames[i]) == 0) {
//                     is_input = 1;
//                     break;
//                 }
//             }

//             if (is_input) {
//                 fprintf(stdout, "设置动态链接器：%s → %s\n", dest_path,
//                         interp_path);
//                 elf_set_interpreter(dest_path, interp_path);
//             }
//         }

//         curr = curr->next;
//     }

//     free(ld_linux_name);
//     return 0;
// }

// /**
//  * 生成启动脚本
//  */
// static int create_startup_scripts() {
//     if (!g_ld_linux) {
//         fprintf(stderr, "错误：未找到动态链接器\n");
//         return -1;
//     }

//     char script_path[PATH_MAX];
//     char *ld_linux_name = safe_basename(g_ld_linux);
//     if (!ld_linux_name) {
//         fprintf(stderr, "错误：无法获取动态链接器文件名\n");
//         return -1;
//     }

//     for (int i = 0; i < g_args.filename_cnt; i++) {
//         char *filename = g_args.filenames[i];
//         if (!filename) {
//             continue;
//         }

//         char *name = safe_basename(filename);
//         if (!name) {
//             continue;
//         }

//         snprintf(script_path, sizeof(script_path), "%s/%s%s", g_args.output,
//                  name, g_args.suffix);
//         fprintf(stdout, "生成启动脚本：%s\n", script_path);

//         if (g_args.dry) {
//             free(name);
//             continue;
//         }

//         FILE *fp = fopen(script_path, "w");
//         if (!fp) {
//             fprintf(stderr, "错误：创建启动脚本 '%s' 失败（%s）\n", script_path,
//                     strerror(errno));
//             free(name);
//             return -1;
//         }

//         // 写入脚本内容（增强兼容性）
//         fprintf(fp, "#!/bin/bash\n");
//         fprintf(fp, "set -eo pipefail\n"); // 更严格的错误处理
//         fprintf(
//             fp,
//             "SCRIPT_DIR=$(cd \"$(dirname \"$0\")\" && pwd -P)\n"); // 绝对路径
//         fprintf(fp, "INTERP=\"$SCRIPT_DIR/%s\"\n", ld_linux_name);
//         fprintf(fp, "EXECUTABLE=\"$SCRIPT_DIR/%s\"\n", name);
//         fprintf(fp, "\n");
//         fprintf(fp, "# 验证文件存在性\n");
//         fprintf(fp, "if [ ! -f \"$INTERP\" ]; then\n");
//         fprintf(fp, "  echo \"错误：动态链接器 '$INTERP' 不存在\" >&2\n");
//         fprintf(fp, "  exit 1\n");
//         fprintf(fp, "fi\n");
//         fprintf(fp, "if [ ! -f \"$EXECUTABLE\" ]; then\n");
//         fprintf(fp, "  echo \"错误：可执行文件 '$EXECUTABLE' 不存在\" >&2\n");
//         fprintf(fp, "  exit 1\n");
//         fprintf(fp, "fi\n");
//         fprintf(fp, "\n");
//         fprintf(fp, "# 确保文件可执行\n");
//         fprintf(fp, "chmod +x \"$INTERP\" 2>/dev/null\n");
//         fprintf(fp, "chmod +x \"$EXECUTABLE\" 2>/dev/null\n");
//         fprintf(fp, "\n");
//         fprintf(fp, "# 运行程序\n");
//         if (!g_args.patch) {
//             fprintf(fp, "LD_LIBRARY_PATH=\"$SCRIPT_DIR:$LD_LIBRARY_PATH\" exec "
//                         "-a \"$0\" \"$INTERP\" \"$EXECUTABLE\" \"$@\"\n");
//         } else {
//             fprintf(fp, "exec -a \"$0\" \"$EXECUTABLE\" \"$@\"\n");
//         }

//         fclose(fp);
//         chmod(script_path, 0755); // 设置执行权限
//         free(name);
//     }

//     free(ld_linux_name);
//     return 0;
// }

// // ====================== 命令行参数解析 ======================
// static void print_help() {
//     printf("用法：mockup [选项] <ELF文件>...\n");
//     printf("功能：将ELF可执行文件及其依赖打包为平台无关的独立目录（纯原生C实现"
//            "，无第三方依赖）\n\n");
//     printf("必选参数：\n");
//     printf("  <ELF文件>...          一个或多个待打包的ELF可执行文件\n\n");
//     printf("可选参数：\n");
//     printf("  -o, --output <目录>   输出目录路径，默认：bin\n");
//     printf("  -f, --force           强制覆盖已存在的输出目录\n");
//     printf("  -D, --dry             干跑模式，仅打印依赖，不执行实际打包\n");
//     printf(
//         "  -P, --patch           启用ELF修改（设置RPATH/SONAME/动态链接器）\n");
//     printf("  -x, --suffix <后缀>   启动脚本后缀，默认：.sh\n");
//     printf("  -h, --help            显示帮助信息并退出\n\n");
//     printf("示例：\n");
//     printf("  1. 基础打包：mockup -o myapp ./myapp\n");
//     printf("  2. 强制覆盖：mockup -f -o myapp ./myapp\n");
//     printf("  3. 启用ELF优化：mockup -P -o myapp ./myapp\n");
//     printf("  4. 干跑测试：mockup -D ./myapp\n");
// }

// static int parse_args(int argc, char *argv[]) {
//     // 初始化默认参数（堆内存，需后续free）
//     g_args.suffix = strdup(".sh");
//     g_args.output = strdup("bin");
//     if (!g_args.suffix || !g_args.output) {
//         fprintf(stderr, "错误：内存分配失败（默认参数）\n");
//         return -1;
//     }

//     // 解析短选项（无长选项依赖，简化实现）
//     int opt;
//     while ((opt = getopt(argc, argv, "o:fDPx:h")) != -1) {
//         switch (opt) {
//         case 'o':
//             free(g_args.output);
//             g_args.output = strdup(optarg);
//             if (!g_args.output) {
//                 fprintf(stderr, "错误：内存分配失败（output目录）\n");
//                 return -1;
//             }
//             break;
//         case 'f': g_args.force = 1; break;
//         case 'D': g_args.dry = 1; break;
//         case 'P': g_args.patch = 1; break;
//         case 'x':
//             free(g_args.suffix);
//             g_args.suffix = strdup(optarg);
//             if (!g_args.suffix) {
//                 fprintf(stderr, "错误：内存分配失败（脚本后缀）\n");
//                 return -1;
//             }
//             break;
//         case 'h': print_help(); exit(0);
//         default:
//             fprintf(stderr, "错误：未知选项 '%c'\n", optopt);
//             print_help();
//             exit(1);
//         }
//     }

//     // 收集输入文件（转换为绝对路径）
//     g_args.filename_cnt = argc - optind;
//     if (g_args.filename_cnt == 0) {
//         fprintf(stderr, "错误：必须指定至少一个ELF文件\n");
//         print_help();
//         exit(1);
//     }

//     g_args.filenames = (char **)malloc(sizeof(char *) * g_args.filename_cnt);
//     if (!g_args.filenames) {
//         fprintf(stderr, "错误：内存分配失败（文件列表）\n");
//         return -1;
//     }

//     for (int i = 0; i < g_args.filename_cnt; i++) {
//         char *abs_path = get_absolute_path(argv[optind + i]);
//         if (!abs_path) {
//             // 释放已分配的内存
//             for (int j = 0; j < i; j++) {
//                 free(g_args.filenames[j]);
//             }
//             free(g_args.filenames);
//             return -1;
//         }
//         g_args.filenames[i] = abs_path;
//     }

//     return 0;
// }

// // ====================== 主函数 ======================
// int main(int argc, char *argv[]) {
//     // 解析命令行参数
//     if (parse_args(argc, argv) != 0) {
//         return 1;
//     }

//     // 处理输出目录
//     if (!g_args.dry) {
//         struct stat st;
//         if (stat(g_args.output, &st) == 0) {
//             if (S_ISDIR(st.st_mode)) {
//                 if (g_args.force) {
//                     fprintf(stdout, "强制删除已存在目录：%s\n", g_args.output);
//                     if (rmdir_recursive(g_args.output) != 0) {
//                         return 1;
//                     }
//                 } else {
//                     fprintf(stderr,
//                             "错误：目录 '%s' 已存在，请使用 -f 强制覆盖\n",
//                             g_args.output);
//                     return 1;
//                 }
//             } else {
//                 fprintf(stderr, "错误：'%s' 不是目录\n", g_args.output);
//                 return 1;
//             }
//         }
//         // 创建输出目录
//         if (mkdir_recursive(g_args.output) != 0) {
//             return 1;
//         }
//     }

//     // 查找所有依赖
//     if (find_all_dependencies() != 0) {
//         return 1;
//     }

//     // 复制依赖文件
//     if (copy_dependencies() != 0) {
//         return 1;
//     }

//     // 可选：修改ELF文件
//     if (g_args.patch) {
//         if (patch_elf_files() != 0) {
//             return 1;
//         }
//     }

//     // 生成启动脚本
//     if (create_startup_scripts() != 0) {
//         return 1;
//     }

//     // 输出完成信息
//     printf("\n=== 打包完成！===\n");
//     printf("输出目录：%s\n", g_args.output);
//     char *script_name = safe_basename(g_args.filenames[0]);
//     if (script_name) {
//         printf("启动脚本：%s/%s%s\n", g_args.output, script_name,
//                g_args.suffix);
//         free(script_name);
//     }
//     printf("使用方法：\n");
//     printf("  1. 进入目录：cd %s\n", g_args.output);
//     printf("  2. 运行脚本：./%s%s\n", safe_basename(g_args.filenames[0]),
//            g_args.suffix);
//     printf("提示：可拷贝整个目录到任意Linux发行版运行（无额外依赖）\n");

//     // 释放所有资源（避免内存泄漏）
//     dep_list_free(g_dep_list);
//     for (int i = 0; i < g_args.filename_cnt; i++) {
//         free(g_args.filenames[i]);
//     }
//     free(g_args.filenames);
//     free(g_args.output);
//     free(g_args.suffix);
//     free(g_ld_linux);

//     return 0;
// }


#include <dirent.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// ====================== 原生C链表（无第三方依赖） ======================
typedef struct DepNode {
    char *name;           // 依赖库文件名（如 libc.so.6）
    char *path;           // 依赖库绝对路径
    struct DepNode *next; // 下一个节点
} DepNode;

// 链表操作函数（纯原生C实现，增加内存检查）
static DepNode *dep_list_create() {
    return NULL;
}

static void dep_list_add(DepNode **head, char const *name, char const *path) {
    if (!name || !path) {
        fprintf(stderr, "警告：无效的依赖库名称或路径，跳过添加\n");
        return;
    }

    DepNode *new_node = (DepNode *)malloc(sizeof(DepNode));
    if (!new_node) {
        fprintf(stderr, "错误：内存分配失败（DepNode）\n");
        return;
    }

    new_node->name = strdup(name);
    new_node->path = strdup(path);
    // 检查strdup是否成功
    if (!new_node->name || !new_node->path) {
        fprintf(stderr, "错误：内存分配失败（strdup）\n");
        free(new_node->name);
        free(new_node->path);
        free(new_node);
        return;
    }

    new_node->next = *head;
    *head = new_node;
}

static DepNode *dep_list_find(DepNode *head, char const *name) {
    if (!name) {
        return NULL;
    }
    DepNode *curr = head;
    while (curr) {
        if (strcmp(curr->name, name) == 0) {
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

static int dep_list_count(DepNode *head) {
    int cnt = 0;
    DepNode *curr = head;
    while (curr) {
        cnt++;
        curr = curr->next;
    }
    return cnt;
}

static void dep_list_free(DepNode *head) {
    DepNode *curr = head;
    while (curr) {
        DepNode *tmp = curr;
        curr = curr->next;
        free(tmp->name);
        free(tmp->path);
        free(tmp);
    }
}

// ====================== 全局配置 ======================
typedef struct {
    char **filenames; // 输入ELF文件列表（绝对路径）
    int filename_cnt; // 输入文件数量
    char *output;     // 输出目录路径
    int force;        // 强制覆盖（-f）
    int dry;          // 干跑模式（-D）
    int patch;        // 启用ELF修改（-P）
    char *suffix;     // 启动脚本后缀（-x，默认.sh）
} Args;

// 全局变量
static Args g_args = {0};
static DepNode *g_dep_list = NULL; // 依赖库链表
static char *g_ld_linux =
    NULL; // 动态链接器路径（如 /lib64/ld-linux-x86-64.so.2）

// ====================== 基础工具函数（纯原生C） ======================
/**
 * 检查文件是否存在且可读
 */
static int file_exists(char const *path) {
    if (!path) {
        return 0;
    }
    return access(path, R_OK) == 0;
}

/**
 * 获取文件绝对路径（失败返回NULL，需调用者free）
 */
static char *get_absolute_path(char const *path) {
    if (!path) {
        return NULL;
    }
    char abs_path[PATH_MAX];
    if (realpath(path, abs_path) == NULL) {
        fprintf(stderr, "错误：无法获取绝对路径 '%s'（%s）\n", path,
                strerror(errno));
        return NULL;
    }
    return strdup(abs_path);
}

/**
 * 安全获取文件名（basename包装，避免free静态缓冲区）
 * 返回值：堆内存字符串，需调用者free
 */
static char *safe_basename(char const *path) {
    if (!path) {
        return NULL;
    }
    // basename可能返回静态缓冲区，不能直接free，需strdup复制
    char *base = basename((char *)path);
    return base ? strdup(base) : NULL;
}

/**
 * 递归创建目录（纯原生C实现）
 */
static int mkdir_recursive(char const *dir) {
    if (!dir) {
        return -1;
    }
    char tmp[PATH_MAX];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", dir);
    len = strlen(tmp);
    if (len == 0) {
        return -1;
    }
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                fprintf(stderr, "错误：创建目录 '%s' 失败（%s）\n", tmp,
                        strerror(errno));
                return -1;
            }
            *p = '/';
        }
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "错误：创建目录 '%s' 失败（%s）\n", tmp,
                strerror(errno));
        return -1;
    }
    return 0;
}

/**
 * 递归删除目录（纯原生C实现）
 */
static int rmdir_recursive(char const *dir) {
    if (!dir) {
        return -1;
    }
    char path[PATH_MAX];
    struct dirent *dp;
    DIR *dirp = opendir(dir);

    if (!dirp) {
        fprintf(stderr, "错误：打开目录 '%s' 失败（%s）\n", dir,
                strerror(errno));
        return -1;
    }

    while ((dp = readdir(dirp)) != NULL) {
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0) {
            continue;
        }

        snprintf(path, sizeof(path), "%s/%s", dir, dp->d_name);
        struct stat st;
        if (stat(path, &st) == -1) {
            fprintf(stderr, "警告：获取文件状态 '%s' 失败（%s）\n", path,
                    strerror(errno));
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            if (rmdir_recursive(path) != 0) {
                closedir(dirp);
                return -1;
            }
        } else {
            if (unlink(path) != 0) {
                fprintf(stderr, "错误：删除文件 '%s' 失败（%s）\n", path,
                        strerror(errno));
                closedir(dirp);
                return -1;
            }
        }
    }

    closedir(dirp);
    if (rmdir(dir) != 0) {
        fprintf(stderr, "错误：删除目录 '%s' 失败（%s）\n", dir,
                strerror(errno));
        return -1;
    }
    return 0;
}

/**
 * 复制文件（纯原生C实现，无第三方依赖）
 */
static int copy_file(char const *src, char const *dest) {
    if (!src || !dest) {
        fprintf(stderr, "错误：源文件或目标文件路径为空\n");
        return -1;
    }

    int src_fd = open(src, O_RDONLY);
    if (src_fd == -1) {
        fprintf(stderr, "错误：打开源文件 '%s' 失败（%s）\n", src,
                strerror(errno));
        return -1;
    }

    int dest_fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0755); // 可执行权限
    if (dest_fd == -1) {
        fprintf(stderr, "错误：创建目标文件 '%s' 失败（%s）\n", dest,
                strerror(errno));
        close(src_fd);
        return -1;
    }

    char buf[4096];
    ssize_t n;
    while ((n = read(src_fd, buf, sizeof(buf))) > 0) {
        if (write(dest_fd, buf, n) != n) {
            fprintf(stderr, "错误：复制文件 '%s'->'%s' 失败（%s）\n", src, dest,
                    strerror(errno));
            close(src_fd);
            close(dest_fd);
            unlink(dest); // 删除不完整的目标文件
            return -1;
        }
    }

    if (n == -1) {
        fprintf(stderr, "错误：读取源文件 '%s' 失败（%s）\n", src,
                strerror(errno));
        close(src_fd);
        close(dest_fd);
        unlink(dest);
        return -1;
    }

    // 复制文件权限（简化：直接设置为0755，确保可执行）
    fchmod(dest_fd, 0755);

    close(src_fd);
    close(dest_fd);
    return 0;
}

// ====================== ELF解析工具函数（纯原生C） ======================
/**
 * 内存映射ELF文件（高效读取）
 * 返回：映射地址，失败返回NULL；size和fd通过参数传出
 */
static void *elf_mmap(char const *path, size_t *size, int *fd) {
    if (!path || !size || !fd) {
        return NULL;
    }
    *fd = open(path, O_RDONLY);
    if (*fd == -1) {
        fprintf(stderr, "错误：打开ELF文件 '%s' 失败（%s）\n", path,
                strerror(errno));
        return NULL;
    }

    struct stat st;
    if (fstat(*fd, &st) == -1) {
        fprintf(stderr, "错误：获取文件状态 '%s' 失败（%s）\n", path,
                strerror(errno));
        close(*fd);
        return NULL;
    }
    *size = st.st_size;

    void *addr = mmap(NULL, *size, PROT_READ, MAP_PRIVATE, *fd, 0);
    if (addr == MAP_FAILED) {
        fprintf(stderr, "错误：映射文件 '%s' 失败（%s）\n", path,
                strerror(errno));
        close(*fd);
        return NULL;
    }

    // 验证ELF标识
    if (memcmp(addr, ELFMAG, SELFMAG) != 0) {
        fprintf(stderr, "错误：'%s' 不是ELF文件\n", path);
        munmap(addr, *size);
        close(*fd);
        return NULL;
    }

    return addr;
}

/**
 * 解除ELF文件内存映射
 */
static void elf_unmap(void *addr, size_t size, int fd) {
    if (addr != MAP_FAILED && addr != NULL) {
        munmap(addr, size);
    }
    if (fd >= 0) {
        close(fd);
    }
}

/**
 * 获取ELF文件的动态链接器路径（PT_INTERP段）
 * 返回：动态链接器路径（堆内存），失败返回NULL，需调用者free
 */
static char *elf_get_interpreter(char const *path) {
    if (!path) {
        return NULL;
    }
    size_t size;
    int fd;
    void *addr = elf_mmap(path, &size, &fd);
    if (!addr) {
        return NULL;
    }

    Elf64_Ehdr *ehdr64 = (Elf64_Ehdr *)addr;
    char *interp = NULL;

    switch (ehdr64->e_ident[EI_CLASS]) {
    case ELFCLASS64: {
        Elf64_Phdr *phdr = (Elf64_Phdr *)(addr + ehdr64->e_phoff);
        for (int i = 0; i < ehdr64->e_phnum; i++) {
            if (phdr[i].p_type == PT_INTERP) {
                interp = strdup((char *)(addr + phdr[i].p_offset));
                break;
            }
        }
        break;
    }
    case ELFCLASS32: {
        Elf32_Ehdr *ehdr32 = (Elf32_Ehdr *)addr;
        Elf32_Phdr *phdr = (Elf32_Phdr *)(addr + ehdr32->e_phoff);
        for (int i = 0; i < ehdr32->e_phnum; i++) {
            if (phdr[i].p_type == PT_INTERP) {
                interp = strdup((char *)(addr + phdr[i].p_offset));
                break;
            }
        }
        break;
    }
    default:
        fprintf(stderr, "错误：不支持的ELF架构（%d）\n",
                ehdr64->e_ident[EI_CLASS]);
    }

    elf_unmap(addr, size, fd);
    return interp;
}

/**
 * 获取ELF文件的依赖库列表（DT_NEEDED）
 * @return 依赖库名称数组（NULL终止），需调用者释放（free每个元素+数组本身）
 */
static char **elf_get_needed(char const *path, int *count) {
    if (!path || !count) {
        return NULL;
    }
    *count = 0;
    size_t size;
    int fd;
    void *addr = elf_mmap(path, &size, &fd);
    if (!addr) {
        return NULL;
    }

    Elf64_Ehdr *ehdr64 = (Elf64_Ehdr *)addr;
    char **needed = NULL;
    char const *dynstr = NULL;

    switch (ehdr64->e_ident[EI_CLASS]) {
    case ELFCLASS64: {
        Elf64_Shdr *shdr = (Elf64_Shdr *)(addr + ehdr64->e_shoff);
        Elf64_Shdr *dyn_shdr = NULL;
        Elf64_Shdr *dynstr_shdr = NULL;

        // 查找.dynamic和.dynstr段
        for (int i = 0; i < ehdr64->e_shnum; i++) {
            if (shdr[i].sh_type == SHT_DYNAMIC) {
                dyn_shdr = &shdr[i];
            } else if (shdr[i].sh_type == SHT_STRTAB) {
                // 查找段名（.dynstr）
                char *sh_name =
                    (char *)(addr + shdr[ehdr64->e_shstrndx].sh_offset +
                             shdr[i].sh_name);
                if (sh_name && strcmp(sh_name, ".dynstr") == 0) {
                    dynstr_shdr = &shdr[i];
                    dynstr = (char *)(addr + dynstr_shdr->sh_offset);
                }
            }
        }

        if (!dyn_shdr || !dynstr_shdr) {
            fprintf(stderr, "警告：'%s' 无动态段（静态链接）\n", path);
            break;
        }

        // 遍历DT_NEEDED条目
        Elf64_Dyn *dyn = (Elf64_Dyn *)(addr + dyn_shdr->sh_offset);
        for (; dyn->d_tag != DT_NULL; dyn++) {
            if (dyn->d_tag == DT_NEEDED) {
                char const *lib_name = dynstr + dyn->d_un.d_val;
                if (!lib_name) {
                    continue;
                }
                needed =
                    (char **)realloc(needed, sizeof(char *) * (*count + 1));
                if (!needed) {
                    fprintf(stderr, "错误：内存分配失败（needed数组）\n");
                    break;
                }
                needed[*count] = strdup(lib_name);
                (*count)++;
            }
        }
        break;
    }
    case ELFCLASS32: {
        Elf32_Ehdr *ehdr32 = (Elf32_Ehdr *)addr;
        Elf32_Shdr *shdr = (Elf32_Shdr *)(addr + ehdr32->e_shoff);
        Elf32_Shdr *dyn_shdr = NULL;
        Elf32_Shdr *dynstr_shdr = NULL;

        for (int i = 0; i < ehdr32->e_shnum; i++) {
            if (shdr[i].sh_type == SHT_DYNAMIC) {
                dyn_shdr = &shdr[i];
            } else if (shdr[i].sh_type == SHT_STRTAB) {
                char *sh_name =
                    (char *)(addr + shdr[ehdr32->e_shstrndx].sh_offset +
                             shdr[i].sh_name);
                if (sh_name && strcmp(sh_name, ".dynstr") == 0) {
                    dynstr_shdr = &shdr[i];
                    dynstr = (char *)(addr + dynstr_shdr->sh_offset);
                }
            }
        }

        if (!dyn_shdr || !dynstr_shdr) {
            fprintf(stderr, "警告：'%s' 无动态段（静态链接）\n", path);
            break;
        }

        Elf32_Dyn *dyn = (Elf32_Dyn *)(addr + dyn_shdr->sh_offset);
        for (; dyn->d_tag != DT_NULL; dyn++) {
            if (dyn->d_tag == DT_NEEDED) {
                char const *lib_name = dynstr + dyn->d_un.d_val;
                if (!lib_name) {
                    continue;
                }
                needed =
                    (char **)realloc(needed, sizeof(char *) * (*count + 1));
                if (!needed) {
                    fprintf(stderr, "错误：内存分配失败（needed数组）\n");
                    break;
                }
                needed[*count] = strdup(lib_name);
                (*count)++;
            }
        }
        break;
    }
    default:
        fprintf(stderr, "错误：不支持的ELF架构（%d）\n",
                ehdr64->e_ident[EI_CLASS]);
    }

    // 添加NULL终止符
    needed = (char **)realloc(needed, sizeof(char *) * (*count + 1));
    if (needed) {
        needed[*count] = NULL;
    }

    elf_unmap(addr, size, fd);
    return needed;
}

/**
 * 查找依赖库的绝对路径（模拟动态链接器规则）
 * 返回：绝对路径（堆内存），失败返回NULL，需调用者free
 */
static char *elf_find_library(char const *lib_name) {
    if (!lib_name || strlen(lib_name) == 0) {
        return NULL;
    }

    // 标准库路径（Linux通用路径）
    char const *std_paths[] = {"/lib",
                               "/usr/lib",
                               "/lib64",
                               "/usr/lib64",
                               "/usr/local/lib",
                               "/usr/local/lib64",
                               "/lib/x86_64-linux-gnu",
                               "/usr/lib/x86_64-linux-gnu", // Debian/Ubuntu
                               "/lib/i386-linux-gnu",
                               "/usr/lib/i386-linux-gnu",
                               "/lib/aarch64-linux-gnu",
                               "/usr/lib/aarch64-linux-gnu", // ARM64
                               NULL};

    // 1. 检查LD_LIBRARY_PATH环境变量
    char *ld_lib_path = getenv("LD_LIBRARY_PATH");
    if (ld_lib_path) {
        char *path_copy = strdup(ld_lib_path);
        if (path_copy) {
            char *dir = strtok(path_copy, ":");
            while (dir) {
                char lib_path[PATH_MAX];
                snprintf(lib_path, sizeof(lib_path), "%s/%s", dir, lib_name);
                if (file_exists(lib_path)) {
                    char *abs_path = get_absolute_path(lib_path);
                    free(path_copy);
                    return abs_path;
                }
                dir = strtok(NULL, ":");
            }
            free(path_copy);
        }
    }

    // 2. 检查标准库路径
    for (int i = 0; std_paths[i]; i++) {
        char lib_path[PATH_MAX];
        snprintf(lib_path, sizeof(lib_path), "%s/%s", std_paths[i], lib_name);
        if (file_exists(lib_path)) {
            return get_absolute_path(lib_path);
        }
    }

    fprintf(stderr, "警告：未找到依赖库 '%s'\n", lib_name);
    return NULL;
}

// ====================== ELF修改工具函数（纯原生C，修复指针类型）
// ======================
/**
 * 修改ELF文件的动态链接器（PT_INTERP段）
 */
static int elf_set_interpreter(char const *path, char const *new_interp) {
    if (!path || !new_interp) {
        fprintf(stderr, "错误：路径或新动态链接器为空\n");
        return -1;
    }

    int fd = open(path, O_RDWR);
    if (fd == -1) {
        fprintf(stderr, "错误：打开ELF文件 '%s' 失败（%s）\n", path,
                strerror(errno));
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) == -1) {
        fprintf(stderr, "错误：获取文件状态 '%s' 失败（%s）\n", path,
                strerror(errno));
        close(fd);
        return -1;
    }

    void *addr =
        mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        fprintf(stderr, "错误：映射文件 '%s' 失败（%s）\n", path,
                strerror(errno));
        close(fd);
        return -1;
    }

    Elf64_Ehdr *ehdr64 = (Elf64_Ehdr *)addr;
    int ret = -1;

    switch (ehdr64->e_ident[EI_CLASS]) {
    case ELFCLASS64: {
        Elf64_Phdr *phdr = (Elf64_Phdr *)(addr + ehdr64->e_phoff);
        for (int i = 0; i < ehdr64->e_phnum; i++) {
            if (phdr[i].p_type == PT_INTERP) {
                // 检查新路径长度是否超出原有空间
                if (strlen(new_interp) >= phdr[i].p_filesz) {
                    fprintf(stderr,
                            "错误：动态链接器路径过长（最大支持 %zu 字节）\n",
                            (size_t)phdr[i].p_filesz - 1);
                    goto out;
                }
                // 覆盖写入新路径
                memset((char *)(addr + phdr[i].p_offset), 0, phdr[i].p_filesz);
                strncpy((char *)(addr + phdr[i].p_offset), new_interp,
                        phdr[i].p_filesz - 1);
                ret = 0;
                break;
            }
        }
        break;
    }
    case ELFCLASS32: {
        Elf32_Ehdr *ehdr32 = (Elf32_Ehdr *)addr;
        Elf32_Phdr *phdr = (Elf32_Phdr *)(addr + ehdr32->e_phoff);
        for (int i = 0; i < ehdr32->e_phnum; i++) {
            if (phdr[i].p_type == PT_INTERP) {
                if (strlen(new_interp) >= phdr[i].p_filesz) {
                    fprintf(stderr,
                            "错误：动态链接器路径过长（最大支持 %zu 字节）\n",
                            (size_t)phdr[i].p_filesz - 1);
                    goto out;
                }
                memset((char *)(addr + phdr[i].p_offset), 0, phdr[i].p_filesz);
                strncpy((char *)(addr + phdr[i].p_offset), new_interp,
                        phdr[i].p_filesz - 1);
                ret = 0;
                break;
            }
        }
        break;
    }
    default:
        fprintf(stderr, "错误：不支持的ELF架构（%d）\n",
                ehdr64->e_ident[EI_CLASS]);
    }

out:
    msync(addr, st.st_size, MS_SYNC); // 同步修改到磁盘
    munmap(addr, st.st_size);
    close(fd);
    return ret;
}

/**
 * 设置ELF文件的RPATH（DT_RPATH/DT_RUNPATH）- 修复指针类型混用
 */
static int elf_set_rpath(char const *path, char const *rpath) {
    if (!path || !rpath) {
        fprintf(stderr, "错误：路径或RPATH为空\n");
        return -1;
    }

    int fd = open(path, O_RDWR);
    if (fd == -1) {
        fprintf(stderr, "错误：打开ELF文件 '%s' 失败（%s）\n", path,
                strerror(errno));
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) == -1) {
        fprintf(stderr, "错误：获取文件状态 '%s' 失败（%s）\n", path,
                strerror(errno));
        close(fd);
        return -1;
    }

    void *addr =
        mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        fprintf(stderr, "错误：映射文件 '%s' 失败（%s）\n", path,
                strerror(errno));
        close(fd);
        return -1;
    }

    Elf64_Ehdr *ehdr64 = (Elf64_Ehdr *)addr;
    int ret = -1;

    switch (ehdr64->e_ident[EI_CLASS]) {
    case ELFCLASS64: {
        Elf64_Shdr *shdr = (Elf64_Shdr *)(addr + ehdr64->e_shoff);
        Elf64_Shdr *dyn_shdr = NULL;    // 64位专用指针
        Elf64_Shdr *dynstr_shdr = NULL; // 64位专用指针

        // 查找.dynamic和.dynstr段
        for (int i = 0; i < ehdr64->e_shnum; i++) {
            if (shdr[i].sh_type == SHT_DYNAMIC) {
                dyn_shdr = &shdr[i];
            } else if (shdr[i].sh_type == SHT_STRTAB) {
                char *sh_name =
                    (char *)(addr + shdr[ehdr64->e_shstrndx].sh_offset +
                             shdr[i].sh_name);
                if (sh_name && strcmp(sh_name, ".dynstr") == 0) {
                    dynstr_shdr = &shdr[i];
                }
            }
        }

        if (!dyn_shdr || !dynstr_shdr) {
            fprintf(stderr, "错误：'%s' 无动态段\n", path);
            goto out;
        }

        // 查找DT_RPATH/DT_RUNPATH条目并修改
        Elf64_Dyn *dyn = (Elf64_Dyn *)(addr + dyn_shdr->sh_offset);
        for (; dyn->d_tag != DT_NULL; dyn++) {
            if (dyn->d_tag == DT_RPATH || dyn->d_tag == DT_RUNPATH) {
                size_t rpath_len = strlen(rpath);
                size_t strtab_size = dynstr_shdr->sh_size;
                size_t str_offset = dyn->d_un.d_val;

                if (str_offset + rpath_len + 1 > strtab_size) {
                    fprintf(stderr, "错误：字符串表空间不足，无法设置RPATH\n");
                    goto out;
                }

                // 写入新RPATH
                memset((char *)(addr + dynstr_shdr->sh_offset + str_offset), 0,
                       rpath_len + 1);
                strncpy((char *)(addr + dynstr_shdr->sh_offset + str_offset),
                        rpath, rpath_len);
                ret = 0;
                break;
            }
        }

        if (dyn->d_tag == DT_NULL) {
            fprintf(stderr,
                    "警告：'%s' 无DT_RPATH/DT_RUNPATH条目，跳过RPATH设置\n",
                    path);
            ret = 0; // 非致命错误
        }
        break;
    }
    case ELFCLASS32: {
        Elf32_Ehdr *ehdr32 = (Elf32_Ehdr *)addr;
        Elf32_Shdr *shdr = (Elf32_Shdr *)(addr + ehdr32->e_shoff);
        Elf32_Shdr *dyn_shdr = NULL;    // 32位专用指针（修复类型混用）
        Elf32_Shdr *dynstr_shdr = NULL; // 32位专用指针（修复类型混用）

        // 查找.dynamic和.dynstr段
        for (int i = 0; i < ehdr32->e_shnum; i++) {
            if (shdr[i].sh_type == SHT_DYNAMIC) {
                dyn_shdr = &shdr[i]; // 现在是同类型赋值，无警告
            } else if (shdr[i].sh_type == SHT_STRTAB) {
                char *sh_name =
                    (char *)(addr + shdr[ehdr32->e_shstrndx].sh_offset +
                             shdr[i].sh_name);
                if (sh_name && strcmp(sh_name, ".dynstr") == 0) {
                    dynstr_shdr = &shdr[i]; // 同类型赋值，无警告
                }
            }
        }

        if (!dyn_shdr || !dynstr_shdr) {
            fprintf(stderr, "错误：'%s' 无动态段\n", path);
            goto out;
        }

        // 查找DT_RPATH/DT_RUNPATH条目并修改
        Elf32_Dyn *dyn = (Elf32_Dyn *)(addr + dyn_shdr->sh_offset);
        for (; dyn->d_tag != DT_NULL; dyn++) {
            if (dyn->d_tag == DT_RPATH || dyn->d_tag == DT_RUNPATH) {
                size_t rpath_len = strlen(rpath);
                size_t strtab_size = dynstr_shdr->sh_size;
                size_t str_offset = dyn->d_un.d_val;

                if (str_offset + rpath_len + 1 > strtab_size) {
                    fprintf(stderr, "错误：字符串表空间不足，无法设置RPATH\n");
                    goto out;
                }

                memset((char *)(addr + dynstr_shdr->sh_offset + str_offset), 0,
                       rpath_len + 1);
                strncpy((char *)(addr + dynstr_shdr->sh_offset + str_offset),
                        rpath, rpath_len);
                ret = 0;
                break;
            }
        }

        if (dyn->d_tag == DT_NULL) {
            fprintf(stderr,
                    "警告：'%s' 无DT_RPATH/DT_RUNPATH条目，跳过RPATH设置\n",
                    path);
            ret = 0;
        }
        break;
    }
    default:
        fprintf(stderr, "错误：不支持的ELF架构（%d）\n",
                ehdr64->e_ident[EI_CLASS]);
    }

out:
    msync(addr, st.st_size, MS_SYNC);
    munmap(addr, st.st_size);
    close(fd);
    return ret;
}

/**
 * 设置ELF文件的SONAME（DT_SONAME）- 修复指针类型混用
 */
static int elf_set_soname(char const *path, char const *soname) {
    if (!path || !soname) {
        fprintf(stderr, "错误：路径或SONAME为空\n");
        return -1;
    }

    int fd = open(path, O_RDWR);
    if (fd == -1) {
        fprintf(stderr, "错误：打开ELF文件 '%s' 失败（%s）\n", path,
                strerror(errno));
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) == -1) {
        fprintf(stderr, "错误：获取文件状态 '%s' 失败（%s）\n", path,
                strerror(errno));
        close(fd);
        return -1;
    }

    void *addr =
        mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        fprintf(stderr, "错误：映射文件 '%s' 失败（%s）\n", path,
                strerror(errno));
        close(fd);
        return -1;
    }

    Elf64_Ehdr *ehdr64 = (Elf64_Ehdr *)addr;
    int ret = -1;

    switch (ehdr64->e_ident[EI_CLASS]) {
    case ELFCLASS64: {
        Elf64_Shdr *shdr = (Elf64_Shdr *)(addr + ehdr64->e_shoff);
        Elf64_Shdr *dyn_shdr = NULL;
        Elf64_Shdr *dynstr_shdr = NULL;

        for (int i = 0; i < ehdr64->e_shnum; i++) {
            if (shdr[i].sh_type == SHT_DYNAMIC) {
                dyn_shdr = &shdr[i];
            } else if (shdr[i].sh_type == SHT_STRTAB) {
                char *sh_name =
                    (char *)(addr + shdr[ehdr64->e_shstrndx].sh_offset +
                             shdr[i].sh_name);
                if (sh_name && strcmp(sh_name, ".dynstr") == 0) {
                    dynstr_shdr = &shdr[i];
                }
            }
        }

        if (!dyn_shdr || !dynstr_shdr) {
            fprintf(stderr, "错误：'%s' 无动态段\n", path);
            goto out;
        }

        // 查找DT_SONAME条目并修改
        Elf64_Dyn *dyn = (Elf64_Dyn *)(addr + dyn_shdr->sh_offset);
        for (; dyn->d_tag != DT_NULL; dyn++) {
            if (dyn->d_tag == DT_SONAME) {
                size_t soname_len = strlen(soname);
                size_t strtab_size = dynstr_shdr->sh_size;
                size_t str_offset = dyn->d_un.d_val;

                if (str_offset + soname_len + 1 > strtab_size) {
                    fprintf(stderr, "错误：字符串表空间不足，无法设置SONAME\n");
                    goto out;
                }

                memset((char *)(addr + dynstr_shdr->sh_offset + str_offset), 0,
                       soname_len + 1);
                strncpy((char *)(addr + dynstr_shdr->sh_offset + str_offset),
                        soname, soname_len);
                ret = 0;
                break;
            }
        }

        if (dyn->d_tag == DT_NULL) {
            fprintf(stderr, "警告：'%s' 无DT_SONAME条目，跳过SONAME设置\n",
                    path);
            ret = 0;
        }
        break;
    }
    case ELFCLASS32: {
        Elf32_Ehdr *ehdr32 = (Elf32_Ehdr *)addr;
        Elf32_Shdr *shdr = (Elf32_Shdr *)(addr + ehdr32->e_shoff);
        Elf32_Shdr *dyn_shdr = NULL;    // 32位专用指针（修复类型混用）
        Elf32_Shdr *dynstr_shdr = NULL; // 32位专用指针（修复类型混用）

        for (int i = 0; i < ehdr32->e_shnum; i++) {
            if (shdr[i].sh_type == SHT_DYNAMIC) {
                dyn_shdr = &shdr[i]; // 同类型赋值，无警告
            } else if (shdr[i].sh_type == SHT_STRTAB) {
                char *sh_name =
                    (char *)(addr + shdr[ehdr32->e_shstrndx].sh_offset +
                             shdr[i].sh_name);
                if (sh_name && strcmp(sh_name, ".dynstr") == 0) {
                    dynstr_shdr = &shdr[i]; // 同类型赋值，无警告
                }
            }
        }

        if (!dyn_shdr || !dynstr_shdr) {
            fprintf(stderr, "错误：'%s' 无动态段\n", path);
            goto out;
        }

        // 查找DT_SONAME条目并修改
        Elf32_Dyn *dyn = (Elf32_Dyn *)(addr + dyn_shdr->sh_offset);
        for (; dyn->d_tag != DT_NULL; dyn++) {
            if (dyn->d_tag == DT_SONAME) {
                size_t soname_len = strlen(soname);
                size_t strtab_size = dynstr_shdr->sh_size;
                size_t str_offset = dyn->d_un.d_val;

                if (str_offset + soname_len + 1 > strtab_size) {
                    fprintf(stderr, "错误：字符串表空间不足，无法设置SONAME\n");
                    goto out;
                }

                memset((char *)(addr + dynstr_shdr->sh_offset + str_offset), 0,
                       soname_len + 1);
                strncpy((char *)(addr + dynstr_shdr->sh_offset + str_offset),
                        soname, soname_len);
                ret = 0;
                break;
            }
        }

        if (dyn->d_tag == DT_NULL) {
            fprintf(stderr, "警告：'%s' 无DT_SONAME条目，跳过SONAME设置\n",
                    path);
            ret = 0;
        }
        break;
    }
    default:
        fprintf(stderr, "错误：不支持的ELF架构（%d）\n",
                ehdr64->e_ident[EI_CLASS]);
    }

out:
    msync(addr, st.st_size, MS_SYNC);
    munmap(addr, st.st_size);
    close(fd);
    return ret;
}

// ====================== 依赖查找核心函数 ======================
/**
 * 递归查找ELF文件的所有依赖库
 */
static int find_dependencies_recursive(char const *path) {
    if (!path) {
        return -1;
    }

    // 安全获取文件名（避免free静态缓冲区）
    char *lib_name = safe_basename(path);
    if (!lib_name) {
        fprintf(stderr, "警告：无法获取文件名 '%s'\n", path);
        return -1;
    }

    // 检查是否已存在于依赖列表
    if (dep_list_find(g_dep_list, lib_name)) {
        free(lib_name); // 释放safe_basename分配的堆内存
        return 0;
    }

    // 获取依赖库列表
    int needed_cnt;
    char **needed = elf_get_needed(path, &needed_cnt);
    if (!needed) {
        free(lib_name);
        return -1;
    }

    // 添加当前文件到依赖列表
    dep_list_add(&g_dep_list, lib_name, path);
    free(lib_name); // 此处必须free（safe_basename的返回值）

    // 递归查找每个依赖
    for (int i = 0; i < needed_cnt; i++) {
        char *dep_name = needed[i];
        if (!dep_name) {
            continue;
        }

        char *dep_path = elf_find_library(dep_name);
        if (dep_path) {
            fprintf(stdout, "找到依赖：%s → %s\n", dep_name, dep_path);
            find_dependencies_recursive(dep_path); // 递归查找子依赖
            free(dep_path);
        }
        free(dep_name); // 释放elf_get_needed分配的堆内存
    }

    free(needed); // 释放依赖数组
    return 0;
}

/**
 * 初始化依赖查找（入口函数）
 */
static int find_all_dependencies() {
    // 初始化依赖列表
    g_dep_list = dep_list_create();

    // 处理每个输入文件
    for (int i = 0; i < g_args.filename_cnt; i++) {
        char *path = g_args.filenames[i];
        if (!path) {
            continue;
        }

        fprintf(stdout, "正在解析：%s\n", path);

        // 验证文件是否存在
        if (!file_exists(path)) {
            fprintf(stderr, "错误：文件 '%s' 不存在或不可读\n", path);
            return -1;
        }

        // 获取动态链接器（仅第一次获取）
        if (!g_ld_linux) {
            g_ld_linux = elf_get_interpreter(path);
            if (!g_ld_linux) {
                fprintf(stderr,
                        "错误：无法获取动态链接器路径（可能是静态链接文件）\n");
                return -1;
            }
            fprintf(stdout, "找到动态链接器：%s\n", g_ld_linux);
        }

        // 递归查找依赖
        find_dependencies_recursive(path);
    }

    fprintf(stdout, "依赖解析完成：共 %d 个依赖库 + 1 个动态链接器\n",
            dep_list_count(g_dep_list));
    return 0;
}

// ====================== 打包核心函数 ======================
/**
 * 复制依赖文件到输出目录
 */
static int copy_dependencies() {
    if (!g_ld_linux) {
        fprintf(stderr, "错误：未找到动态链接器\n");
        return -1;
    }

    char dest_path[PATH_MAX];
    char *ld_linux_name = safe_basename(g_ld_linux);
    if (!ld_linux_name) {
        fprintf(stderr, "错误：无法获取动态链接器文件名\n");
        return -1;
    }

    // 复制动态链接器
    snprintf(dest_path, sizeof(dest_path), "%s/%s", g_args.output,
             ld_linux_name);
    fprintf(stdout, "复制动态链接器：%s → %s\n", g_ld_linux, dest_path);
    if (!g_args.dry) {
        if (copy_file(g_ld_linux, dest_path) != 0) {
            free(ld_linux_name);
            return -1;
        }
    }

    // 复制依赖库
    DepNode *curr = g_dep_list;
    while (curr) {
        snprintf(dest_path, sizeof(dest_path), "%s/%s", g_args.output,
                 curr->name);
        fprintf(stdout, "复制依赖库：%s → %s\n", curr->path, dest_path);
        if (!g_args.dry) {
            copy_file(curr->path, dest_path);
        }
        curr = curr->next;
    }

    free(ld_linux_name);
    return 0;
}

/**
 * 修改ELF文件（RPATH/SONAME/INTERP）- 已修复字符串拼接
 */
static int patch_elf_files() {
    if (!g_ld_linux) {
        fprintf(stderr, "错误：未找到动态链接器\n");
        return -1;
    }

    char dest_path[PATH_MAX];
    char *ld_linux_name = safe_basename(g_ld_linux);
    char interp_path[PATH_MAX]; // 动态链接器路径缓冲区
    if (!ld_linux_name) {
        fprintf(stderr, "错误：无法获取动态链接器文件名\n");
        return -1;
    }

    // 拼接 "./动态链接器名"（正确的字符串拼接方式）
    snprintf(interp_path, sizeof(interp_path), "./%s", ld_linux_name);

    DepNode *curr = g_dep_list;
    while (curr) {
        snprintf(dest_path, sizeof(dest_path), "%s/%s", g_args.output,
                 curr->name);
        fprintf(stdout, "修改ELF文件：%s\n", dest_path);

        if (!g_args.dry) {
            // 设置RPATH为当前目录（$ORIGIN表示ELF文件所在目录）
            elf_set_rpath(dest_path, "$ORIGIN");

            // 设置SONAME（仅SO库）
            if (strstr(curr->name, ".so") != NULL) {
                elf_set_soname(dest_path, curr->name);
            }

            // 检查是否为输入文件，设置动态链接器
            int is_input = 0;
            for (int i = 0; i < g_args.filename_cnt; i++) {
                if (strcmp(curr->path, g_args.filenames[i]) == 0) {
                    is_input = 1;
                    break;
                }
            }

            if (is_input) {
                fprintf(stdout, "设置动态链接器：%s → %s\n", dest_path,
                        interp_path);
                elf_set_interpreter(dest_path, interp_path);
            }
        }

        curr = curr->next;
    }

    free(ld_linux_name);
    return 0;
}

/**
 * 生成启动脚本
 */
static int create_startup_scripts() {
    if (!g_ld_linux) {
        fprintf(stderr, "错误：未找到动态链接器\n");
        return -1;
    }

    char script_path[PATH_MAX];
    char *ld_linux_name = safe_basename(g_ld_linux);
    if (!ld_linux_name) {
        fprintf(stderr, "错误：无法获取动态链接器文件名\n");
        return -1;
    }

    for (int i = 0; i < g_args.filename_cnt; i++) {
        char *filename = g_args.filenames[i];
        if (!filename) {
            continue;
        }

        char *name = safe_basename(filename);
        if (!name) {
            continue;
        }

        snprintf(script_path, sizeof(script_path), "%s/%s%s", g_args.output,
                 name, g_args.suffix);
        fprintf(stdout, "生成启动脚本：%s\n", script_path);

        if (g_args.dry) {
            free(name);
            continue;
        }

        FILE *fp = fopen(script_path, "w");
        if (!fp) {
            fprintf(stderr, "错误：创建启动脚本 '%s' 失败（%s）\n", script_path,
                    strerror(errno));
            free(name);
            return -1;
        }

        // 写入脚本内容（增强兼容性）
        fprintf(fp, "#!/bin/bash\n");
        fprintf(fp, "set -eo pipefail\n"); // 更严格的错误处理
        fprintf(
            fp,
            "SCRIPT_DIR=$(cd \"$(dirname \"$0\")\" && pwd -P)\n"); // 绝对路径
        fprintf(fp, "INTERP=\"$SCRIPT_DIR/%s\"\n", ld_linux_name);
        fprintf(fp, "EXECUTABLE=\"$SCRIPT_DIR/%s\"\n", name);
        fprintf(fp, "\n");
        fprintf(fp, "# 验证文件存在性\n");
        fprintf(fp, "if [ ! -f \"$INTERP\" ]; then\n");
        fprintf(fp, "  echo \"错误：动态链接器 '$INTERP' 不存在\" >&2\n");
        fprintf(fp, "  exit 1\n");
        fprintf(fp, "fi\n");
        fprintf(fp, "if [ ! -f \"$EXECUTABLE\" ]; then\n");
        fprintf(fp, "  echo \"错误：可执行文件 '$EXECUTABLE' 不存在\" >&2\n");
        fprintf(fp, "  exit 1\n");
        fprintf(fp, "fi\n");
        fprintf(fp, "\n");
        fprintf(fp, "# 确保文件可执行\n");
        fprintf(fp, "chmod +x \"$INTERP\" 2>/dev/null\n");
        fprintf(fp, "chmod +x \"$EXECUTABLE\" 2>/dev/null\n");
        fprintf(fp, "\n");
        fprintf(fp, "# 运行程序\n");
        if (!g_args.patch) {
            fprintf(fp, "LD_LIBRARY_PATH=\"$SCRIPT_DIR:$LD_LIBRARY_PATH\" exec "
                        "-a \"$0\" \"$INTERP\" \"$EXECUTABLE\" \"$@\"\n");
        } else {
            fprintf(fp, "exec -a \"$0\" \"$EXECUTABLE\" \"$@\"\n");
        }

        fclose(fp);
        chmod(script_path, 0755); // 设置执行权限
        free(name);
    }

    free(ld_linux_name);
    return 0;
}

// ====================== 命令行参数解析 ======================
static void print_help() {
    printf("用法：mockup [选项] <ELF文件>...\n");
    printf("功能：将ELF可执行文件及其依赖打包为平台无关的独立目录（纯原生C实现"
           "，无第三方依赖）\n\n");
    printf("必选参数：\n");
    printf("  <ELF文件>...          一个或多个待打包的ELF可执行文件\n\n");
    printf("可选参数：\n");
    printf("  -o, --output <目录>   输出目录路径，默认：bin\n");
    printf("  -f, --force           强制覆盖已存在的输出目录\n");
    printf("  -D, --dry             干跑模式，仅打印依赖，不执行实际打包\n");
    printf(
        "  -P, --patch           启用ELF修改（设置RPATH/SONAME/动态链接器）\n");
    printf("  -x, --suffix <后缀>   启动脚本后缀，默认：.sh\n");
    printf("  -h, --help            显示帮助信息并退出\n\n");
    printf("示例：\n");
    printf("  1. 基础打包：mockup -o myapp ./myapp\n");
    printf("  2. 强制覆盖：mockup -f -o myapp ./myapp\n");
    printf("  3. 启用ELF优化：mockup -P -o myapp ./myapp\n");
    printf("  4. 干跑测试：mockup -D ./myapp\n");
}

static int parse_args(int argc, char *argv[]) {
    // 初始化默认参数（堆内存，需后续free）
    g_args.suffix = strdup(".sh");
    g_args.output = strdup("bin");
    if (!g_args.suffix || !g_args.output) {
        fprintf(stderr, "错误：内存分配失败（默认参数）\n");
        return -1;
    }

    // 解析短选项（无长选项依赖，简化实现）
    int opt;
    while ((opt = getopt(argc, argv, "o:fDPx:h")) != -1) {
        switch (opt) {
        case 'o':
            free(g_args.output);
            g_args.output = strdup(optarg);
            if (!g_args.output) {
                fprintf(stderr, "错误：内存分配失败（output目录）\n");
                return -1;
            }
            break;
        case 'f': g_args.force = 1; break;
        case 'D': g_args.dry = 1; break;
        case 'P': g_args.patch = 1; break;
        case 'x':
            free(g_args.suffix);
            g_args.suffix = strdup(optarg);
            if (!g_args.suffix) {
                fprintf(stderr, "错误：内存分配失败（脚本后缀）\n");
                return -1;
            }
            break;
        case 'h': print_help(); exit(0);
        default:
            fprintf(stderr, "错误：未知选项 '%c'\n", optopt);
            print_help();
            exit(1);
        }
    }

    // 收集输入文件（转换为绝对路径）
    g_args.filename_cnt = argc - optind;
    if (g_args.filename_cnt == 0) {
        fprintf(stderr, "错误：必须指定至少一个ELF文件\n");
        print_help();
        exit(1);
    }

    g_args.filenames = (char **)malloc(sizeof(char *) * g_args.filename_cnt);
    if (!g_args.filenames) {
        fprintf(stderr, "错误：内存分配失败（文件列表）\n");
        return -1;
    }

    for (int i = 0; i < g_args.filename_cnt; i++) {
        char *abs_path = get_absolute_path(argv[optind + i]);
        if (!abs_path) {
            // 释放已分配的内存
            for (int j = 0; j < i; j++) {
                free(g_args.filenames[j]);
            }
            free(g_args.filenames);
            return -1;
        }
        g_args.filenames[i] = abs_path;
    }

    return 0;
}

// ====================== 主函数 ======================
int main(int argc, char *argv[]) {
    // 解析命令行参数
    if (parse_args(argc, argv) != 0) {
        return 1;
    }

    // 处理输出目录
    if (!g_args.dry) {
        struct stat st;
        if (stat(g_args.output, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                if (g_args.force) {
                    fprintf(stdout, "强制删除已存在目录：%s\n", g_args.output);
                    if (rmdir_recursive(g_args.output) != 0) {
                        return 1;
                    }
                } else {
                    fprintf(stderr,
                            "错误：目录 '%s' 已存在，请使用 -f 强制覆盖\n",
                            g_args.output);
                    return 1;
                }
            } else {
                fprintf(stderr, "错误：'%s' 不是目录\n", g_args.output);
                return 1;
            }
        }
        // 创建输出目录
        if (mkdir_recursive(g_args.output) != 0) {
            return 1;
        }
    }

    // 查找所有依赖
    if (find_all_dependencies() != 0) {
        return 1;
    }

    // 复制依赖文件
    if (copy_dependencies() != 0) {
        return 1;
    }

    // 可选：修改ELF文件
    if (g_args.patch) {
        if (patch_elf_files() != 0) {
            return 1;
        }
    }

    // 生成启动脚本
    if (create_startup_scripts() != 0) {
        return 1;
    }

    // 输出完成信息
    printf("\n=== 打包完成！===\n");
    printf("输出目录：%s\n", g_args.output);
    char *script_name = safe_basename(g_args.filenames[0]);
    if (script_name) {
        printf("启动脚本：%s/%s%s\n", g_args.output, script_name,
               g_args.suffix);
        free(script_name);
    }
    printf("使用方法：\n");
    printf("  1. 进入目录：cd %s\n", g_args.output);
    printf("  2. 运行脚本：./%s%s\n", safe_basename(g_args.filenames[0]),
           g_args.suffix);
    printf("提示：可拷贝整个目录到任意Linux发行版运行（无额外依赖）\n");

    // 释放所有资源（避免内存泄漏）
    dep_list_free(g_dep_list);
    for (int i = 0; i < g_args.filename_cnt; i++) {
        free(g_args.filenames[i]);
    }
    free(g_args.filenames);
    free(g_args.output);
    free(g_args.suffix);
    free(g_ld_linux);

    return 0;
}