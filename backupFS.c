#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <utime.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>

#ifndef O_NOATIME
#define O_NOATIME     01000000
#endif

#define INCRBUF 1024
#define NUMARQ 20
#define aloca(var,tipo,unidades) do { var = (tipo*) malloc (sizeof (tipo) * (unidades)); } while (var == NULL);
#define libera(var) { free ((void*) var); var = NULL; }

char *junta_string (char *, char *);
int apaga_recursivo (char *);
int define_atributos (char *, struct stat *, struct stat *);
int copia_item (char *, char *);
int main (int, char **);

char *junta_string (char *string1, char *string2) {
    char *r;
    aloca (r, char, 1 + strlen (string1) + strlen (string2));
    strcpy (r, string1);
    strcat (r, string2);
    return (r);
}

int apaga_recursivo (char *pasta) {
    struct stat it;
    char *tmpbuff, *pathname;
    DIR *d;
    struct dirent *d_entry;
    int estado, lpasta, r;
    estado = 0;
    lpasta = strlen (pasta);
    d = opendir (pasta);
    if (d == NULL) {
        fprintf (stderr, "\nErro abrindo pasta '%s': ", pasta);
        perror (NULL);
        return (1);
    }
    while ((d_entry = readdir (d)) != NULL) {
        if (strcmp (d_entry->d_name, ".") && strcmp (d_entry->d_name, "..")) {
            tmpbuff = junta_string ("/", d_entry->d_name);
            pathname = junta_string (pasta, tmpbuff);
            libera (tmpbuff);
            r = lstat (pathname, &it);
            if (r) {
                estado = 1;
                fprintf (stderr, "\nErro obtendo informacoes sobre o item '%s': ", pathname);
            } else if (S_ISDIR(it.st_mode)) {
                if (apaga_recursivo (pathname)) {
                    estado = 1;
                }
            } else {
                r = unlink (pathname);
                if (r) {
                    fprintf (stderr, "\nErro removendo arquivo '%s': ", pathname);
                    estado = 1;
                }
            }
            libera (pathname);
        }
    }
    closedir (d);
    r = rmdir (pasta);
    if (r) {
        fprintf (stderr, "\nErro removendo pasta '%s': ", pasta);
        perror (NULL);
        estado = 1;
    }
    return (estado);
}

int define_atributos (char *arquivo, struct stat *origattr, struct stat *attr) {
    int r, rmax, muda_dono, muda_grupo, muda_permissao, muda_mtime;
    struct utimbuf t;
    rmax = 0;
    muda_dono = 1;
    muda_grupo = 1;
    muda_permissao = 1;
    muda_mtime = 1;
    if (origattr != NULL) {
        muda_permissao = (origattr->st_mode ^ attr->st_mode) & 07777;
        muda_dono = origattr->st_uid - attr->st_uid;
        muda_grupo = origattr->st_gid - attr->st_gid;
        muda_mtime = origattr->st_mtime - attr->st_mtime;
        if (S_ISLNK (origattr->st_mode)) {
            muda_permissao = 0;
            muda_mtime = 0;
        }
    }
    if (muda_dono || muda_grupo) {
        r = lchown (arquivo, (muda_dono ? attr->st_uid : -1), (muda_grupo ? attr->st_gid : -1));
        if (r) {
            fprintf (stderr, "\nErro definindo propriedades para '%s': ", arquivo);
            perror (NULL);
            rmax = 1;
        }
    }
    if (muda_permissao && (! S_ISLNK(attr->st_mode))) {
        r = chmod (arquivo, attr->st_mode);
        if (r) {
            fprintf (stderr, "\nErro definindo permissoes para '%s': ", arquivo);
            perror (NULL);
            rmax = 1;
        }
    }
    if (muda_mtime && (! S_ISLNK(attr->st_mode))) {
        t.actime = attr->st_atime;
        t.modtime = attr->st_mtime;
        r = utime (arquivo, &t);
        if (r) {
            fprintf (stderr, "\nErro definindo datas de acesso e modificacao para '%s': ", arquivo);
            perror (NULL);
            rmax = 1;
        }
    }
    return (rmax);
}

int copia_item (char *origem, char *destino) {
    /* Destino eh o caminho da pasta de destino */
    /* 0 indica sucesso */
    struct stat orig, dest, dest2, aremover;
    struct dirent *d_entry;
    DIR *dd;
    int r, l, qua;
    char *nome, *tmp, *tmp2;
    static int agrupamentos = 0;
    if (agrupamentos++ > NUMARQ) {
        putc ('.', stderr);
        agrupamentos = 0;
    }
    nome = basename (origem);
    if (! (strcmp (nome, "") && strcmp (nome, ".") && strcmp (nome, ".."))) {
        fprintf (stderr, "\n'%s': argumento invalido.\n", origem);
        return (1);
    }
    r = lstat (origem, &orig);
    if (r) {
        fprintf (stderr, "\nErro abrindo '%s': ", origem);
        perror (NULL);
        return (1);
    }
    if (! (S_ISDIR (orig.st_mode) || S_ISREG (orig.st_mode) || S_ISLNK (orig.st_mode))) {
        /* fprintf (stderr, "\nAviso: '%s' nao eh uma pasta, arquivo ou link simbolico. Nao sera copiado.\n", origem); */
        return (0);
    }
    r = lstat (destino, &dest);
    if (r) {
        fprintf (stderr, "\nErro abrindo '%s': ", destino);
        perror (NULL);
        return (1);
    }
    if (! (S_ISDIR (dest.st_mode))) {
        fprintf (stderr, "\nErro: '%s' nao eh uma pasta.\n", destino);
        return (1);
    }
    tmp = junta_string ("/", nome);
    nome = junta_string (destino, tmp);
    libera (tmp);
    r = lstat (nome, &dest2);
    qua = 1;
    if (r) {
        if (errno != ENOENT) {
            fprintf (stderr, "\nErro obtendo informacoes sobre '%s': ", nome);
            perror (NULL);
            libera (nome);
            return (1);
        }
    } else {
        /* O arquivo de destino ja existe */
        if (S_ISREG (dest2.st_mode) || S_ISLNK (dest2.st_mode)) {
            if ((S_ISREG(dest2.st_mode) && S_ISREG(orig.st_mode)) || ((S_ISLNK(dest2.st_mode) && S_ISLNK(orig.st_mode)))) {
                if (dest2.st_mtime >= orig.st_mtime && dest2.st_size == orig.st_size) {
                    r = define_atributos (nome, &dest2, &orig);
                    libera (nome);
                    return (r);
                }
            }
            r = unlink (nome);
            if (r) {
                fprintf (stderr, "\nErro removendo arquivo '%s': ", nome);
                libera (nome);
                return (1);
            }
        } else if (S_ISDIR (dest2.st_mode)) {
            if (S_ISDIR (orig.st_mode)) {
                qua = 0;
            } else {
                r = apaga_recursivo (nome);
                if (r) {
                    fprintf (stderr, "\nErro removendo pasta '%s': ", nome);
                    libera (nome);
                    return (1);
                }
            }
        } else {
            r = unlink (nome);
            if (r) {
                fprintf (stderr, "\nErro: '%s' nao pode ser sobreescrito.", nome);
                libera (nome);
                return (1);
            }
        }
    }
    if (S_ISLNK (orig.st_mode)) {
        l = 0;
        while (1) {
            l += INCRBUF;
            aloca (tmp, char, l);
            r = readlink (origem, tmp, l);
            if (r == -1) {
                fprintf (stderr, "\nErro obtendo caminho apontado pelo link simbolico '%s': ", origem);
                perror (NULL);
                libera (tmp);
                libera (nome);
                return (1);
            } else if (r == l) {
                libera (tmp);
            } else {
                break;
            }
        }
        tmp[r] = '\0';
        l = r;
        r = symlink (tmp, nome);
        if (r == -1) {
            fprintf (stderr, "\nErro criando link de '%s' para '%s': ", nome, tmp);
            perror (NULL);
            libera (tmp)
            libera (nome);
            return (1);
        }
        libera (tmp);
        r = define_atributos (nome, NULL, &orig);
        libera (nome);
        return (r);
    } else if (S_ISREG (orig.st_mode)) {
        l = creat (nome, S_IRWXU);
        if (l == -1) {
            fprintf (stderr, "\nErro criando arquivo '%s': ", nome);
            perror (NULL);
            libera (nome);
            return (1);
        }
        r = open (origem, O_RDONLY | O_NOATIME);
        if (r == -1) {
            fprintf (stderr, "\nErro lendo arquivo '%s': ", origem);
            perror (NULL);
            libera (nome);
            close (l);
            return (1);
        }
        aloca (tmp, char, INCRBUF);
        while ((qua = read (r, tmp, INCRBUF)) != 0) {
            if (qua == -1) {
                fprintf (stderr, "\nErro lendo arquivo '%s': ", origem);
                perror (NULL);
                libera (nome);
                libera (tmp);
                close (l);
                close (r);
                return (1);
            } else if (write (l, tmp, qua) == -1) {
                fprintf (stderr, "\nErro gravando arquivo '%s': ", nome);
                perror (NULL);
                libera (nome);
                libera (tmp);
                close (l);
                close (r);
                return (1);
            }
        }
        close (l);
        close (r);
        libera (tmp);
        r = define_atributos (nome, NULL, &orig);
        libera (nome);
        return (r);
    } else {
        if (qua) {
            dest2.st_mode = S_IRWXU;
            dest2.st_uid = geteuid ();
            dest2.st_gid = getegid ();
            dest2.st_mtime = time (NULL);
            dest2.st_atime = time (NULL);
            r = mkdir (nome, S_IRWXU);
            if (r == -1) {
                fprintf (stderr, "\nErro criando pasta '%s': ", nome);
                perror (NULL);
                libera (nome);
                return (1);
            }
        }
        qua = 0;
        dd = opendir (origem);
        if (dd != NULL) {
            while ((d_entry = readdir (dd)) != NULL) {
                if (strcmp (d_entry->d_name, ".") && strcmp (d_entry->d_name, "..")) {
                    tmp2 = junta_string ("/", d_entry->d_name);
                    tmp = junta_string (origem, tmp2);
                    if (copia_item (tmp, nome)) {
                        qua = 1;
                    }
                    libera (tmp2);
                    libera (tmp);
                }
            }
            closedir (dd);
        } else {
            fprintf (stderr, "\nErro abrindo pasta '%s': ", origem);
            perror (NULL);
            qua = 1;
        }
        dd = opendir (nome);
        if (dd != NULL) {
            while ((d_entry = readdir (dd)) != NULL) {
                if (strcmp (d_entry->d_name, ".") && strcmp (d_entry->d_name, "..")) {
                    tmp2 = junta_string ("/", d_entry->d_name);
                    tmp = junta_string (origem, tmp2);
                    libera (tmp2);
                    r = lstat (tmp, &aremover);
                    if (r) {
                        if (errno == ENOENT) {
                            tmp2 = junta_string ("/", d_entry->d_name);
                            tmp = junta_string (nome, tmp2);
                            libera (tmp2);
                            r = lstat (tmp, &aremover);
                            if (r) {
                                fprintf (stderr, "\nErro obtendo informacoes sobre o item '%s': ", tmp);
                                perror (NULL);
                                qua = 1;
                            } else if (S_ISDIR (aremover.st_mode)) {
                                if (apaga_recursivo (tmp)) {
                                    qua = 1;
                                }
                            } else if (unlink (tmp)) {
                                fprintf (stderr, "\nErro removendo '%s': ", tmp);
                                perror (NULL);
                                qua = 1;
                            }
                        } else {
                            fprintf (stderr, "\nErro obtendo informacoes sobre o item '%s': ", tmp);
                            perror (NULL);
                            qua = 1;
                        }
                    }
                    libera (tmp);
                }
            }
            closedir (dd);
        } else {
            fprintf (stderr, "\nErro abrindo pasta '%s': ", nome);
            perror (NULL);
            qua = 1;
        }
        r = define_atributos (nome, &dest2, &orig);
        libera (nome);
        return (r || qua);
    }
}

int main (int argc, char **argv) {
    int m;
    DIR *dd;
    struct dirent *d_entry;
    char *pasta, *tmp1, *tmp2;
 
    if (geteuid ()) {
        fprintf (stderr, "Aviso: este programa deveria ser executado pelo superusuario.\nAlgumas operacoes (por exemplo, mudanca de dono de arquivo) podem ser negadas pelo sistema.\n");
    }
    if (argc != 3) {
        fprintf (stderr, "Uso: %s <pasta_origem> <pasta_destino>\n", argv[0]);
        return (1);
    } else {
        pasta = argv[2];
        m = strlen (pasta);
        if (m == 0) {
            fprintf (stderr, "Argumento invalido.\n");
            return (1);
        }
        if (pasta[m-1] == '/') {
            fprintf (stderr, "Argumento invalido.\n");
            return (1);
        }
        pasta = argv[1];
        m = strlen (pasta);
        if (m == 0) {
            fprintf (stderr, "Argumento invalido.\n");
            return (1);
        }
        if (pasta[m-1] == '/') {
            fprintf (stderr, "Argumento invalido.\n");
            return (1);
        }
        dd = opendir (pasta);
        if (dd == NULL) {
            fprintf (stderr, "Erro abrindo pasta '%s': ", pasta);
            perror (NULL);
            return (1);
        } else {
            m = 0;
            fprintf (stderr, "Fazendo backup de '%s' para '%s'...", pasta, argv[2]);
            while ((d_entry = readdir (dd)) != NULL) {
                if (strcmp (d_entry->d_name, ".") && strcmp (d_entry->d_name, "..")) {
                    tmp1 = junta_string ("/", d_entry->d_name);
                    tmp2 = junta_string (pasta, tmp1);
                    libera (tmp1);
                    if (copia_item (tmp2, argv[2])) {
                        m = 1;
                    }
                    libera (tmp2);
                }
            }
            if (m) {
                fprintf (stderr, "\nConcluido, porem, com saida de erro.\n");
                return (1);
            } else {
                fprintf (stderr, "\nConcluido.\n");
                return (0);
            }
        }
    }
}
