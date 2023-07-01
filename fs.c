#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <string.h>
#include <unistd.h>

#include "fs.h"

int NLINKS;
//======================== FUNÇÕES AUXILIARES ========================//
/*
Operations Modes
0: Procura e retorna pelo arq/dir no caminho fname.
   ex: /path/name , retorna o inode de file
1: Procura e retorna o diretorio pai do caminho.
   ex: /path/file , retorna o inode de path

*/
// Retorna o índice do bloco do arquivo que tenha o nome fname.
uint64_t find_block(struct superblock *sb, const char *fname, int opmode)
{
	char lastbar[strlen(fname)+1];
	if(opmode == 1)
	{
		strcpy(lastbar,fname);
		char* c = strrchr(lastbar,'/');
		*c= '\0';
		if(strlen(lastbar) == 0) return 2; //retorna o endereço da raiz
		fname = lastbar;
	}

	// Fila dos blocos a serem percorridos.
	uint64_t* fila =  (uint64_t*) malloc (sb->blks * sizeof(uint64_t));
	// Fila que marca se um bloco foi visitado ou não.
	int* visitado = (int*) malloc (sb->blks * sizeof(int));
	int inicio = 0, fim = 0, i, aux;

	// Zerando as duas filas.
	for(i = 0; i < sb->blks; i++)
	{
		fila[i] = 0;
		visitado[i] = 0;
	}
	// Colocando o inode pasta raiz no início da fila, marcando ela
	// como visitada e incrementando o tamanho da fila.
	fila[inicio] = sb->root;
	visitado[sb->root] = 1;
	fim++;

	struct inode *in = (struct inode*) calloc(sb->blksz,1);
	struct nodeinfo *ni = (struct nodeinfo*) calloc(sb->blksz,1);
	while(inicio < fim)
	{
		// Colocando o ponteiro na posição indicada pelo início da fila.
		lseek(sb->fd, (fila[inicio] * sb->blksz), SEEK_SET);
		// Lendo os dados do início da fila.
		aux = read(sb->fd, in, sb->blksz);
		// Se o inode in for de um arquivo regular (não é filho).
		if(in->mode == IMREG)
		{
			// Posicionando o ponteiro na posição do nodeinfo.
			lseek(sb->fd, ((in->meta)*sb->blksz), SEEK_SET);
			// Lendo o nodeinfo.
			aux = read(sb->fd, ni, sb->blksz);
			// Se o nome do arquivo é igual ao parâmetro proocurado.
			if(strcmp(ni->name, fname) == 0)
			{
				// Libera os recursos e retorna o índice no FS.
				aux = fila[inicio];
				free(fila);
				free(visitado);
				free(ni);
				free(in);
				return aux;
			}
		}
		// Se o inode in for de uma pasta.
		if(in->mode == IMDIR)
		{
			// Posicionando o ponteiro na posição do nodeinfo.
			lseek(sb->fd, ((in->meta)*sb->blksz), SEEK_SET);
			// Lendo o nodeinfo.
			aux = read(sb->fd, ni, sb->blksz);

			// Se o nome do arquivo é igual ao parâmetro proocurado.
			if(strcmp(ni->name, fname) == 0)
			{
				// Libera os recursos e retorna o índice no FS.
				aux = fila[inicio];
				free(fila);
				free(visitado);
				free(ni);
				free(in);
				return aux;
			}

			// Para cada elemento da pasta.
			for(i = 0; i < NLINKS; i++)
			{
				// Se esse elemento não foi visitado.
				if(visitado[in->links[i]] == 0)
				{
					if(in->links[i] != 0)
					{

						// Marca ele como visitado.
						visitado[in->links[i]] = 1;
						// Insere ele no final da fila.
						fila[fim] = in->links[i];
						// Incrementa o final da fila.
						fim++;
					}
				}
			}
		}
		inicio++;
	}

	free(fila);
	free(visitado);
	free(ni);
	free(in);
	// Caso erro, retorna -1. //(uint64_t)-1 n tem negativo :v
	return 0;
}

/*
*
*
*/
int link_block(struct superblock *sb, struct inode *in, uint64_t in_n, uint64_t block)
{
	int ii;
	uint64_t aux,iaux_n,n;
	struct inode *iaux = (struct inode*) calloc (sb->blksz,1);

	if(in->next == 0)
	{
		//Percorre para axar um local vazio
		for(ii=0; ii<NLINKS; ii++)
		{
			if(in->links[ii] == 0)
			{
				in->links[ii] = block;
				free(iaux);
				return 0;
			}
		}

		//Cria um novo inode
		n = fs_get_block(sb);
		if(n == (uint64_t)-1)
		{
			free(iaux);
			return -1;
		}
		in->next = n;
		iaux->mode = IMCHILD;
		iaux->parent = in_n;
		iaux->next = 0;
		iaux->meta = in_n;
		iaux->links[0] = block;

		//escreve o novo inode
		lseek(sb->fd, n*sb->blksz, SEEK_SET);
		aux = write(sb->fd, iaux, sb->blksz);
		free(iaux);
		if(aux == -1) return -1;
		return 0;
	}

	while(in->next != 0)
	{
		iaux_n = in->next;
		lseek(sb->fd, iaux_n*sb->blksz, SEEK_SET);
		aux = read(sb->fd, iaux, sb->blksz);
		in = iaux;
	}

	//Percorre para axar um local vazio
	for(ii=0; ii<NLINKS; ii++)
	{
		if(in->links[ii] == 0)
		{
			in->links[ii] = block;
			//escreve o inode de volta
			lseek(sb->fd, iaux_n*sb->blksz, SEEK_SET);
			aux = write(sb->fd, iaux, sb->blksz);
			free(iaux);
			return 0;
		}
	}

	//Cria um novo inode
	n = fs_get_block(sb);
	if(n == (uint64_t)-1)
	{
		free(iaux);
		return -1;
	}
	in->next = n;
	iaux->mode = IMCHILD;
	iaux->parent = in_n;
	iaux->next = 0;
	iaux->meta = iaux_n;
	iaux->links[0] = block;

	//escreve o novo inode
	lseek(sb->fd, n*sb->blksz, SEEK_SET);
	aux = write(sb->fd, iaux, sb->blksz);

	free(iaux);
	if(aux == -1) return -1;
	return 0;
}
//====================================================================//

/* Build a new filesystem image in =fname (the file =fname should be
 * present in the OS's filesystem).  The new filesystem should use
 * =blocksize as its block size; the number of blocks in the filesystem
 * will be automatically computed from the file size.  The filesystem
 * will be initialized with an empty root directory.  This function
 * returns NULL on error and sets errno to the appropriate error code.
 * If the block size is smaller than MIN_BLOCK_SIZE bytes, then the
 * format fails and the function sets errno to EINVAL.  If there is
 * insufficient space to store MIN_BLOCK_COUNT blocks in =fname, then
 * the function fails and sets errno to ENOSPC. */
struct superblock * fs_format(const char *fname, uint64_t blocksize)
{
	NLINKS = (blocksize - (4 * sizeof(uint64_t)))/sizeof(uint64_t);
	// Verifica se o tamanho do bloco é menor que o mínimo.
	if(blocksize < MIN_BLOCK_SIZE)
	{
		errno = EINVAL;
		return NULL;
	}

	// Calcula o tamanho do arquivo fname.
	FILE* fp = fopen(fname, "r");
	long fsize;

	if(fp != NULL)
	{
		fseek(fp, 0, SEEK_END);
		fsize = ftell(fp);
	}
	fclose(fp);

	// Verifica se o número de blocos é menor que o mínimo.
	int blocknum = fsize / blocksize;
	if(blocknum < MIN_BLOCK_COUNT)
	{
		errno = ENOSPC;
		return NULL;
	}

	// Declara o superbloco.
	struct superblock* sb = (struct superblock*) malloc (blocksize);
	sb->magic = 0xdcc605f5; // Definição.
	sb->blks = blocknum; // Número de blocos do sistema de arquivos.
	sb->blksz = blocksize; // Tamanho dos blocos (bytes).

	// Em um sistema de arquivos vazio existe o superbloco, o nodeinfo
	// da pasta raiz e o inode da pasta raiz, isso ocupa 3 blocos.
	// Número de blocos livres no sistema de arquivos.
	sb->freeblks = blocknum-3;
	// Apontador para o primeiro bloco livre.
	sb->freelist = 3;
	// Apontador para o inode da pasta raiz.
	sb->root = 2;
	// Descritor de arquivos do FS.
	sb->fd = open(fname, O_RDWR, S_IWRITE | S_IREAD);
	if(sb->fd == -1)
	{
		errno = EBADF;
		free(sb);
		return NULL;
	}

	// Inicializando o superbloco.
	int aux = write(sb->fd, sb, sb->blksz);
	if(aux == -1)
	{
		close(sb->fd);
		free(sb);
		return NULL;
	}

	// Inicializando a pasta raiz.
	struct nodeinfo* root_info = (struct nodeinfo*) calloc (sb->blksz,1);
	root_info->size = 0;
	strcpy(root_info->name, "/\0");
	aux = write(sb->fd, root_info, sb->blksz);
	free(root_info);

	struct inode* root_inode = (struct inode*) calloc (sb->blksz,1);
	root_inode->mode = IMDIR;
	root_inode->parent = 0;
	root_inode->meta = 1;
	root_inode->next = 0;
	aux = write(sb->fd, root_inode, sb->blksz);
	free(root_inode);

	// Inicializando a lista de blocos vazios.
	struct freepage* root_fp = (struct freepage*)
							   malloc (sizeof(struct freepage*));
	for(int i = sb->freelist; i < sb->blks; i++)
	{
		if(i == (sb->blks - 1))
		{
			root_fp->next = 0; // Última pagina livre.
		}
		else
		{
			root_fp->next = i+1;
		}
		aux = write(sb->fd, root_fp, sb->blksz);
	}
	free(root_fp);

	return sb;
}

/* Open the filesystem in =fname and return its superblock. Returns NULL
 * on error, and sets errno accordingly.  If =fname does not contain a
 * 0xdcc605f5, then errno is set to EBADF. */
struct superblock * fs_open(const char *fname)
{
	// Pega o descritor de arquivo do FS.
	int fd = open(fname, O_RDWR);

	// Aplica uma trava exclusiva no arquivo (LOCK_EX = exclusive lock).
	// Apenas um processo poderá usar esse arquivo de cada vez.
	// flock retorna 0 se sucesso, -1 se erro.
	if((flock(fd, LOCK_EX | LOCK_NB)) == -1)
	{
		errno = EBUSY;
		close(fd);
		return NULL;
	}

	// Movo o leitor para a posição inicial do arquivo.
	lseek(fd, 0, SEEK_SET);

	// Carrego o superbloco do FS.
	struct superblock* block = (struct superblock*)
							   malloc(sizeof(struct superblock));
	if(read(fd, block, sizeof(struct superblock)) == -1 )
	{
		close(fd);
		free(block);
		return NULL;
	}


	// Verifico o erro EBADF.
	if(block->magic != 0xdcc605f5)
	{
		// LOCK_UN: remove a trava do arquivo.
		flock(fd, LOCK_UN | LOCK_NB);
		close(fd);
		errno = EBADF;
		free(block);
		return NULL;
	}

	return block;
}

/* Close the filesystem pointed to by =sb.  Returns zero on success and
 * a negative number on error. If there is an error, all resources are
 * freed and errno is set appropriately. */
int fs_close(struct superblock *sb)
{
	// Verifica o descritor do sistema de arquivos.
	if(sb->magic != 0xdcc605f5)
	{
		errno = EBADF;
		return -1;
	}

	// LOCK_UN: remove a trava do arquivo.
	if(flock(sb->fd, LOCK_UN | LOCK_NB) == -1)
	{
		errno = EBUSY;
		return -1;
	}

	// Fechando o arquivo.
	int aux = close(sb->fd);
	if(aux == -1) return -1;
	free(sb);

	return 0;
}

/* Get a free block in the filesystem.  This block shall be removed from
 * the list of free blocks in the filesystem.  If there are no free
 * blocks, zero is returned.  If an error occurs, (uint64_t)-1 is
 * returned and errno is set appropriately. */
uint64_t fs_get_block(struct superblock *sb)
{
	// Verifica o descritor do sistema de arquivos.
	if(sb->magic != 0xdcc605f5)
	{
		errno = EBADF;
		return (uint64_t) 0;
	}

	// Verificando se há blocos livres.
	if(sb->freeblks == 0)
	{
		errno = ENOSPC;
		return (uint64_t) 0;
	}

	struct freepage *page = (struct freepage*) calloc (sb->blksz,1);
	// Localizando posição do primeiro bloco livre.
	lseek(sb->fd, (sb->freelist * sb->blksz), SEEK_SET);
	// Verificando se há algum erro na leitura
	int aux = read(sb->fd, page, sb->blksz);
	if(aux == -1)
	{
		free(page);
		return (uint64_t) 0;
	}
	// Pegando o "ponteiro" do bloco a ser retornado.
	uint64_t block = sb->freelist;
	// Mudando o ponteiro de lista vazia para o próximo bloco livre.
	sb->freelist = page->next;
	// Decrementando a quantidade de blocos livres.
	sb->freeblks--;

	// Escrevendo os novos dados do super bloco (freelist e freeblks).
	lseek(sb->fd, 0, SEEK_SET);
	aux = write(sb->fd, sb, sb->blksz);
	if(aux == -1)
	{
		free(page);
		return (uint64_t) 0;
	}

	free(page);
	return block;
}

/* Put =block back into the filesystem as a free block.  Returns zero on
 * success or a negative value on error.  If there is an error, errno is
 * set accordingly. */
int fs_put_block(struct superblock *sb, uint64_t block)
{
	// Verifica o descritor do sistema de arquivos.
	if(sb->magic != 0xdcc605f5)
	{
		errno = EBADF;
		return -1;
	}

	// Novo bloco a ser inserido na lista de blocos livres.
	struct freepage *page = (struct freepage*) malloc (sb->blksz);

	// Setando os ponteiros do bloco, a nova posição da tabela de blocos
	// livres e incrementando a quantidade de blocos livres.
	page->next = sb->freelist;
	sb->freelist = block;
	sb->freeblks++;

	// Escrevendo os novos dados do super bloco (freelist e freeblks).
	lseek(sb->fd, 0, SEEK_SET);
	int aux = write(sb->fd, sb, sb->blksz);
	if(aux == -1)
	{
		free(page);
		return -1;
	}

	// Escrevendo o bloco (page) no arquivo.
	lseek(sb->fd, block * sb->blksz, SEEK_SET);
	aux = write(sb->fd, page, sb->blksz);

	free(page);
	if(aux == -1) return -1;
	return 0;
}

int fs_write_file(struct superblock *sb, const char *fname, char *buf,
				  size_t cnt)
{
	int ii,aux;
	// Verifica o descritor do sistema de arquivos.
	if(sb->magic != 0xdcc605f5)
	{
		errno = EBADF;
		return -1;
	}

	// Verifica se o nome do arquivo (caminho) é maior que o permitido.
	if(strlen(fname) > ((sb->blksz) - (8*sizeof(uint64_t))))
	{
		errno = ENAMETOOLONG;
		return -1;
	}

	uint64_t file_n, curr_n;
	uint64_t parentdir_n = find_block(sb,fname, 1);
	if(parentdir_n == 0) return -1; //por erro
	struct inode *parentdir = (struct inode*) calloc(sb->blksz,1);
	struct inode *file = (struct inode*) calloc(sb->blksz,1);
	struct inode *auxin = (struct inode*) calloc(sb->blksz,1);
	struct nodeinfo *fileni = (struct nodeinfo*) calloc(sb->blksz,1);
	struct nodeinfo *parentni = (struct nodeinfo*) calloc(sb->blksz,1);

	// Verifica se o arquivo existe no FS.
	uint64_t oldfile_n = find_block(sb, fname, 0);
	if(oldfile_n > 0)
	{
		if(fs_unlink(sb,fname) == -1)
		{
			free(parentdir);
			free(file);
			free(auxin);
			free(fileni);
			free(parentni);
			return -1;
		}
		file_n = fs_get_block(sb);
		if(file_n == (uint64_t)-1)
		{
			free(parentdir);
			free(file);
			free(auxin);
			free(fileni);
			free(parentni);
			errno = ENOSPC;
			return -1;
		}

		//Procura pela referencia do arq no diretorio e atualiza para o novo arq
		curr_n = parentdir_n;
		do
		{
			lseek(sb->fd, curr_n*sb->blksz, SEEK_SET);
			aux = read(sb->fd, auxin, sb->blksz);
			for(ii=0; ii<NLINKS; ii++)
			{
				if(auxin->links[ii] == oldfile_n)
				{
					auxin->links[ii] = file_n;
					lseek(sb->fd, curr_n*sb->blksz, SEEK_SET);
					aux = write(sb->fd, auxin, sb->blksz);
					break;
				}
			}
			curr_n = auxin->next;
		}
		while(curr_n != 0);
	}
	//Se o arq n existia
	else
	{
		//Pega um novo bloco
		file_n = fs_get_block(sb);
		if(file_n == (uint64_t)-1)
		{
			free(parentdir);
			free(file);
			free(auxin);
			free(fileni);
			free(parentni);
			errno = ENOSPC;
			return -1;
		}

		//Lê o nodeinfo e inode do dir pai e atualiza o nodeinfo
		lseek(sb->fd, parentdir_n*sb->blksz, SEEK_SET);
		aux = read(sb->fd, parentdir, sb->blksz);
		lseek(sb->fd, parentdir->meta*sb->blksz, SEEK_SET);
		aux = read(sb->fd, parentni, sb->blksz);
		parentni->size++;
		lseek(sb->fd, parentdir->meta*sb->blksz, SEEK_SET);
		aux = write(sb->fd,parentni,sb->blksz);

		//Linka o novo bloco no dir pai
		link_block(sb,parentdir,parentdir_n,file_n);

		//Escreve o inode do pai
		lseek(sb->fd, parentdir_n*sb->blksz, SEEK_SET);
		aux = write(sb->fd,parentdir,sb->blksz);
	}

	//Cria estrutura do novo arq
	file->parent = parentdir_n;
	file->mode = IMREG;
	file->next = 0;

	//Pega novo bloco pro meta do arq
	file->meta = fs_get_block(sb);
	if(file->meta == (uint64_t)-1)
	{
		free(parentdir);
		free(file);
		free(auxin);
		free(fileni);
		free(parentni);
		errno = ENOSPC;
		return -1;
	}

	//Cria estrutura do meta do arq e a escreve
	strcpy(fileni->name,fname);
	fileni->size = cnt; //Tamanho do nodeinfo e do inode entra no size?
	lseek(sb->fd, file->meta*sb->blksz, SEEK_SET);
	aux = write(sb->fd,fileni,sb->blksz);

	//PARTE 2
	//Cria blocos e Escreve o dado
	//Limpa auxin
	memset(auxin,0,sb->blksz);
	uint64_t block_n;
	uint64_t bytes_left = (uint64_t) cnt*sizeof(char);
	void *block = calloc(sb->blksz,1);

	uint64_t last_n;
	curr_n = file_n;
	free(auxin);
	auxin = file;
	int flageof = 0;
	do
	{
		for(ii = 0; ii<NLINKS && !flageof; ii++)
		{
			if(auxin->links[ii] == 0)
			{
				//Limpa o bloco
				memset(block,0,sb->blksz);
				//Copia os dados ao bloco, mas memcpy n detecta o \0, entao qnd
				//o q restou do buf for menor q blksz, tem de ler so akilo de bytes
				if(bytes_left >= sb->blksz)
				{
					memcpy(block,buf,sb->blksz);
					bytes_left -= sb->blksz;
				}
				else
				{
					memcpy(block,buf, bytes_left);
					flageof = 1;
				}
				//Pega um indice pro bloco
				block_n = fs_get_block(sb);
				if(block_n == (uint64_t) -1)
				{
					free(parentdir);
					free(file);
					free(fileni);
					free(parentni);
					errno = ENOSPC;
					return -1;
				}

				//Linka o bloco
				auxin->links[ii] = block_n;

				//Escreve o bloco
				lseek(sb->fd, block_n*sb->blksz, SEEK_SET);
				aux = write(sb->fd,block,sb->blksz);
			}
		}
		if(flageof)
		{
			//Escreve o inode corrente
			lseek(sb->fd, curr_n*sb->blksz, SEEK_SET);
			aux = write(sb->fd,auxin,sb->blksz);
		}
		else
		{
			//Inode cheio, e eof n encontrado

			last_n = curr_n;
			auxin->next = fs_get_block(sb);
			if(auxin->next == (uint64_t) -1)
			{
				free(parentdir);
				free(file);
				free(fileni);
				free(parentni);
				errno = ENOSPC;
				return -1;
			}

			//Escreve o inode corrente
			lseek(sb->fd, curr_n*sb->blksz, SEEK_SET);
			aux = write(sb->fd,auxin,sb->blksz);

			//Limpa o struct auxin e atualiza curr_n
			curr_n = auxin->next;
			memset(auxin,0,sb->blksz);

			//Cria estrutura do inode filho
			auxin->parent = file_n;
			auxin->meta = last_n;
			auxin->mode = IMCHILD;
			auxin->next = 0;
		}
	}
	while(!flageof);
	free(parentdir);
	free(file);
	free(fileni);
	free(parentni);
	return 0;
}

ssize_t fs_read_file(struct superblock *sb, const char *fname,
					 char *buf, size_t bufsz)
{
	// Verifica o descritor do sistema de arquivos.
	if(sb->magic != 0xdcc605f5)
	{
		errno = EBADF;
		return -1;
	}

	// Verifica se o nome do arquivo (caminho) é maior que o permitido.
	if(strlen(fname) > ((sb->blksz) - (8*sizeof(uint64_t))))
	{
		errno = ENAMETOOLONG;
		return -1;
	}

	// Verifica se o arquivo existe no FS e salvo o "endereço" dele.
	uint64_t block = find_block(sb, fname, 0);
	if(block == 0)
	{
		errno = ENOENT;
		return -1;
	}

	struct inode *in = (struct inode*) calloc(sb->blksz,1);
	struct nodeinfo *ni = (struct nodeinfo*) calloc(sb->blksz,1);
	int aux, bufaux, nlinks, i, mod;
	char* reader = (char*) malloc (sb->blksz);
	// Posiciono o ponteiro na posição do inode do arquivo que vou ler.
	lseek(sb->fd, block * sb->blksz, SEEK_SET);
	// Carrego o inode.
	aux = read(sb->fd, in, sb->blksz);

	// Verifica se o arquivo não é uma pasta/diretório.
	if(in->mode == IMDIR)
	{
		errno = EISDIR;
		free(in);
		free(ni);
		free (reader);
		return -1;
	}

	// Posiciono o ponteiro na posição do nodeinfo desse arquivo.
	lseek(sb->fd, in->meta * sb->blksz, SEEK_SET);
	// Carrego o nodeinfo.
	aux = read(sb->fd, ni, sb->blksz);

	// Quantos links tem 1 inode cheio.
	nlinks = (sb->blksz - 4 * sizeof(uint64_t)) / sizeof(uint64_t);
	// bufaux salva quantos bytes foream lidos.
	bufaux = 0;

	// Enquanto houver mais inodes e não passou do bufsz.
	while(in->next > 0 && bufaux < bufsz)
	{
		// Para todos os links do inode, se não passou de bufsz.
		for(i = 0; i < nlinks && bufaux < bufsz; i++)
		{
			// Posiciono no link[i]
			lseek(sb->fd, in->links[i] * sb->blksz, SEEK_SET);
			// Leio o link[i] numa variavel auxiliar reader.
			aux = read(sb->fd, reader, sb->blksz);
			// Concateno a reader com o buf.
			strcat(buf, reader);
			// Atualizo a quantidade de bytes lidos.
			bufaux += sizeof(sb->blksz);
		}
		// Posiciono e leio o próximo inode.
		lseek(sb->fd, in->next * sb->blksz, SEEK_SET);
		aux = read(sb->fd, in, sb->blksz);
	}
	// No último inode.
	int nlinks_uin = ((ni->size % sb->blksz) / sizeof(uint64_t)) - 4;
	for(i = 0; i < nlinks_uin && bufaux < bufsz; i++)
	{
		// Se o bufsz for múltiplo de sb->blksz.
		if(((uint64_t) bufsz) % (sb->blksz) == 0)
		{
			// Posiciono no link[i]
			lseek(sb->fd, in->links[i] * sb->blksz, SEEK_SET);
			// Leio o link[i] numa variavel auxiliar reader.
			aux = read(sb->fd, reader, sb->blksz);
			// Concateno a reader com o buf.
			strcat(buf, reader);
			// Atualizo a quantidade de bytes lidos.
			bufaux += sb->blksz;
		}
		else // Se o bufsz não for múltiplo de sb->blksz.
		{
			// Calculo o mod em bufsz e sb->blksz.
			mod = ((uint64_t) bufsz) % (sb->blksz);
			// Posiciono no link[i]
			lseek(sb->fd, in->links[i] * sb->blksz, SEEK_SET);
			// Leio o link[i] numa variavel auxiliar reader.
			aux = read(sb->fd, reader, mod);
			// Concateno a reader com o buf.
			strcat(buf, reader);
			// Atualizo a quantidade de bytes lidos.
			bufaux += mod;
		}
	}
	free(in);
	free(ni);
	free (reader);
	return bufaux;
}

int fs_unlink(struct superblock *sb, const char *fname)
{
	// Verifica o descritor do sistema de arquivos.
	if(sb->magic != 0xdcc605f5)
	{
		errno = EBADF;
		return -1;
	}

	// Verifica se o nome do arquivo (caminho) é maior que o permitido.
	if(strlen(fname) > ((sb->blksz) - (8*sizeof(uint64_t))))
	{
		errno = ENAMETOOLONG;
		return -1;
	}

	// Verifica se o arquivo existe no FS.
	uint64_t block = find_block(sb, fname, 0);
	if(block == 0)
	{
		errno = ENOENT;
		return -1;
	}

	struct inode *curr_in = (struct inode*) calloc(sb->blksz,1);
	struct inode *next_in = (struct inode*) calloc(sb->blksz,1);
	struct inode *parentdir = (struct inode*) calloc(sb->blksz,1);
	struct nodeinfo *parentni = (struct nodeinfo*) calloc(sb->blksz,1);
	struct nodeinfo *ni = (struct nodeinfo*) calloc(sb->blksz,1);
	int aux;
	int i, index;




	// Posicionando o ponteiro na posição do primeiro inode para ler.
	lseek(sb->fd, block*sb->blksz, SEEK_SET);
	aux = read(sb->fd, curr_in, sb->blksz);

	// Verifica se é uma pasta.
	if(curr_in->mode == IMDIR)
	{
		errno = EISDIR;
		free(curr_in);
		free(next_in);
		free(parentdir);
		free(parentni);
		free(ni);
		return -1;
	}

	//Le o inode diretorio pai
	lseek(sb->fd, curr_in->parent*sb->blksz, SEEK_SET);
	aux = read(sb->fd, parentdir, sb->blksz);

	//Le o nodeinfo diretorio pai e atualiza-o
	lseek(sb->fd, parentdir->meta*sb->blksz, SEEK_SET);
	aux = read(sb->fd, parentni, sb->blksz);
	parentni->size--;
	lseek(sb->fd, parentdir->meta*sb->blksz, SEEK_SET);
	aux = write(sb->fd, parentni, sb->blksz);

	uint64_t curr_n;
	struct inode *auxin = (struct inode*) calloc(sb->blksz,1);
	int ii;
	//Procura pela referencia do arq no diretorio pai e remove-a
	curr_n = curr_in->parent;
	do
	{
		lseek(sb->fd, curr_n*sb->blksz, SEEK_SET);
		aux = read(sb->fd, auxin, sb->blksz);
		for(ii=0; ii<NLINKS; ii++)
		{
			if(auxin->links[ii] == block)
			{
				auxin->links[ii] = 0;
				lseek(sb->fd, curr_n*sb->blksz, SEEK_SET);
				aux = write(sb->fd, auxin, sb->blksz);
				break;
			}
		}
		curr_n = auxin->next;
	}
	while(curr_n != 0);

	free(auxin);
	free(parentdir);
	free(parentni);

	// Lendo o nodeinfo para pegar o tamanho do arquivo.
	lseek(sb->fd, curr_in->meta * sb->blksz, SEEK_SET);
	aux = read(sb->fd, ni, sb->blksz);
	// Pegando o número de bytes do arquivo.
	//int fsize = ni->size;

	// Liberando o nodeinfo desse arquivo.
	fs_put_block(sb, curr_in->meta);

	// Libero os links utilizados
	for(i = 0; i < NLINKS; i++)
	{
		if(curr_in->links[i] > 0)
		{
			fs_put_block(sb, curr_in->links[i]);
		}
	}



	// Se houver child.
	while(curr_in->next > 0)
	{
		// Salvo o indice do próximo inode.
		index = curr_in->next;
		// Leio o próximo inode.
		lseek(sb->fd, curr_in->next*sb->blksz, SEEK_SET);
		aux = read(sb->fd, curr_in, sb->blksz);


		// Libero os links utilizados
		for(i = 0; i < NLINKS; i++)
		{
			if(curr_in->links[i] > 0)
			{
				fs_put_block(sb, curr_in->links[i]);
			}
		}

		//Libero o inode
		fs_put_block(sb,index);
	}

	// Libero o inode deste arq.
	fs_put_block(sb, block);

	// Caso sucesso.
	free(curr_in);
	free(next_in);
	free(ni);
	return 0;
}

int fs_mkdir(struct superblock *sb, const char *dname)
{
	int aux;
	// Verifica o descritor do sistema de arquivos.
	if(sb->magic != 0xdcc605f5)
	{
		errno = EBADF;
		return -1;
	}

	// Verifica se o nome do arquivo (caminho) é maior que o permitido.
	if(strlen(dname) > ((sb->blksz) - (8*sizeof(uint64_t))))
	{
		errno = ENAMETOOLONG;
		return -1;
	}

	//Erro se o caminho dname nao comeca com \ e se tem espaco
	if((*dname != '/') || (strchr(dname,' ') != NULL) )
	{
		errno = ENOENT;
		return -1;
	}

	//Erro se o diretorio ja existe
	uint64_t block = find_block(sb, dname, 0);
	if(block > 0)
	{
		errno = EEXIST;
		return -1;
	}

	uint64_t parent_n = find_block(sb,dname, 1);
	//Erro, dir pai n existe
	if(parent_n == 0)
	{
		errno = ENOENT;
		return -1;
	}

	//Cria estrutura inode e nodeinfo do novo diretorio
	uint64_t dir_n,dirni_n;
	dir_n = fs_get_block(sb);
	dirni_n = fs_get_block(sb);
	if(dirni_n == (uint64_t)-1 || dir_n > (uint64_t)-1) return -1;
	struct inode *parentdir = (struct inode*) calloc(sb->blksz,1);
	struct inode *dir = (struct inode*) calloc(sb->blksz,1);
	struct nodeinfo *dirni = (struct nodeinfo*) calloc(sb->blksz,1);
	struct nodeinfo *parent_ni = (struct nodeinfo*) calloc(sb->blksz,1);
	dir->mode = IMDIR;
	dir->next = 0;
	dir->parent = parent_n;
	dir->meta = dirni_n;
	char *auxc = strrchr(dname,'/');
	strcpy(dirni->name,auxc);
	dirni->size = 0;

	//Le o inode diretorio pai
	lseek(sb->fd, parent_n*sb->blksz, SEEK_SET);
	aux = read(sb->fd, parentdir, sb->blksz);

	//linka o inode do novo dir ao dir pai
	link_block(sb,parentdir,parent_n,dir_n);

	//Le o nodeinfo diretorio pai para alterar o numero de arquivos e escreve de volta
	lseek(sb->fd, parentdir->meta*sb->blksz, SEEK_SET);
	aux = read(sb->fd, parent_ni, sb->blksz);
	parent_ni->size++;
	lseek(sb->fd, parentdir->meta*sb->blksz, SEEK_SET);
	aux = write(sb->fd,parent_ni,sb->blksz);

	//escreve de volta o inode do pai
	lseek(sb->fd, parent_n*sb->blksz, SEEK_SET);
	aux = write(sb->fd,parentdir,sb->blksz);

	//Escreve o inode e nodeinfo do novo diretorio
	lseek(sb->fd, dir_n*sb->blksz, SEEK_SET);
	aux = write(sb->fd,dir,sb->blksz);
	lseek(sb->fd, dirni_n*sb->blksz, SEEK_SET);
	aux = write(sb->fd,dirni,sb->blksz);

	free(parentdir);
	free(parent_ni);
	free(dir);
	free(dirni);
	return 0;
}

int fs_rmdir(struct superblock *sb, const char *dname)
{
	int aux,ii;
	// Verifica o descritor do sistema de arquivos.
	if(sb->magic != 0xdcc605f5)
	{
		errno = EBADF;
		return -1;
	}

	// Verifica se o nome do arquivo (caminho) é maior que o permitido.
	if(strlen(dname) > ((sb->blksz) - (8*sizeof(uint64_t))))
	{
		errno = ENAMETOOLONG;
		return -1;
	}

	//Erro se o caminho dname nao comeca com \ e se tem espaco
	if((*dname != '/') || (strchr(dname,' ') != NULL) )
	{
		errno = ENOENT;
		return -1;
	}

	//Erro se o diretorio nao existe
	uint64_t block = find_block(sb, dname, 0);
	if(block == 0)
	{
		errno = ENOENT;
		return -1;
	}

	uint64_t parent_n = find_block(sb,dname, 1);
	uint64_t curr_n;
	struct inode *parentdir = (struct inode*) calloc(sb->blksz,1);
	struct inode *dir = (struct inode*) calloc(sb->blksz,1);
	struct nodeinfo *dirni = (struct nodeinfo*) calloc(sb->blksz,1);
	struct nodeinfo *parent_ni = (struct nodeinfo*) calloc(sb->blksz,1);

	//Le o inode do bloco
	lseek(sb->fd, block*sb->blksz, SEEK_SET);
	aux = read(sb->fd, dir, sb->blksz);;

	//Le o nodeinfo do bloco
	lseek(sb->fd, dir->meta * sb->blksz, SEEK_SET);
	aux = read(sb->fd, dirni, sb->blksz);

	// Verificando se a pasta não está vazia:
	if(dirni->size > 0)
	{
		errno = ENOTEMPTY;
		free(parentdir);
		free(parent_ni);
		free(dir);
		free(dirni);
		return -1;
	}

	fs_put_block(sb,dir->meta); //deleta o nodeinfo do bloco
	fs_put_block(sb,block); //deleta o inode do bloco
	memset(dir,0,sb->blksz); //limpa o dir


	//Arruma as info do nodeinfo e inode pai
	lseek(sb->fd, parent_n*sb->blksz, SEEK_SET);
	aux = read(sb->fd, parentdir, sb->blksz);
	lseek(sb->fd, parentdir->meta*sb->blksz, SEEK_SET);
	aux = read(sb->fd, parent_ni, sb->blksz);
	parent_ni->size--;
	lseek(sb->fd, parentdir->meta*sb->blksz, SEEK_SET);
	aux = write(sb->fd,parent_ni,sb->blksz);


	//Procura pela referencia ao diretorio no pai e delta-a
	curr_n = parent_n;
	do
	{
		lseek(sb->fd, curr_n*sb->blksz, SEEK_SET);
		aux = read(sb->fd, dir, sb->blksz);
		for(ii=0; ii<NLINKS; ii++)
		{
			if(dir->links[ii] == block)
			{
				dir->links[ii] = 0;
				lseek(sb->fd, curr_n*sb->blksz, SEEK_SET);
				aux = write(sb->fd, dir, sb->blksz);
				break;
			}
		}
		curr_n = dir->next;
	}
	while(curr_n != 0);


	free(parentdir);
	free(parent_ni);
	free(dir);
	free(dirni);
	return 0;
}

char * fs_list_dir(struct superblock *sb, const char *dname)
{
	// Verifica o descritor do sistema de arquivos.
	if(sb->magic != 0xdcc605f5)
	{
		errno = EBADF;
		return NULL;
	}

	// Verifica se o nome do arquivo (caminho) é maior que o permitido.
	if(strlen(dname) > ((sb->blksz) - (8*sizeof(uint64_t))))
	{
		errno = ENAMETOOLONG;
		return NULL;
	}

	// Procura o indice do inode de dname, e verifica se dname existe.
	uint64_t block = find_block(sb, dname, 0);
	if(block == 0)
	{
		errno = ENOENT;
		return NULL;
	}

	struct inode *in = (struct inode*) calloc(sb->blksz,1);
	struct inode *in_aux = (struct inode*) calloc(sb->blksz,1);
	struct nodeinfo *ni = (struct nodeinfo*) calloc(sb->blksz,1);
	struct nodeinfo *ni_aux = (struct nodeinfo*) calloc(sb->blksz,1);
	int aux, i;
	char* ret = (char*) calloc (500, sizeof(char));

	char* tok;
	char name[50];

	// Posiciono e leio o inode de dname.
	lseek(sb->fd, block * sb->blksz, SEEK_SET);
	aux = read(sb->fd, in, sb->blksz);

	// Verifica se o caminho dname aponta para um diretório.
	if(in->mode != IMDIR)
	{
		free(in);
		free(in_aux);
		free(ni);
		free(ni_aux);
		free(ret);
		errno = ENOTDIR;
		return NULL;
	}

	// Posicionando e lendo o nodeinfo do diretório dname.
	lseek(sb->fd, in->meta * sb->blksz, SEEK_SET);
	aux = read(sb->fd, ni, sb->blksz);

	// Percorrendo os links do diretório dname.
	for(i = 0; i < NLINKS; i++)
	{
		if(in->links[i] != 0)
		{
			// Leio o inode de cada arquivo/pasta dentro do diretorio dname.
			lseek(sb->fd, in->links[i] * sb->blksz, SEEK_SET);
			aux = read(sb->fd, in_aux, sb->blksz);
			// Leio o nodeinfo desse inode.
			lseek(sb->fd, in_aux->meta * sb->blksz, SEEK_SET);
			aux = read(sb->fd, ni_aux, sb->blksz);
			// Pego o nome completo desse arquivo/pasta e divido ela em
			// substrings divididas pela /. Salvo a última parte.
			tok = strtok(ni_aux->name, "/");
			while(tok != NULL)
			{
				strcpy(name, tok);
				tok = strtok(NULL, "/");
			}
			if(in_aux->mode == IMDIR)
				strcat(name, "/");

			// Concateno o nome do arquivo/pasta com a string.
			strcat(ret, name);
			// Acrescento um espaço entre os arquivos/pastas.
			if(i+1<ni->size) strcat(ret, " ");
		}
	}
	free(in);
	free(in_aux);
	free(ni);
	free(ni_aux);
	return ret;
}
