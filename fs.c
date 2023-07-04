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

/*
Retorna o índice do bloco do arquivo que tenha o nome fname
*/
uint64_t encontraBloco(struct superblock *sb, const char *fname, int opmode) {
	char lastbar[strlen(fname) + 1];
	if (opmode == 1) {
		strcpy(lastbar, fname);
		char* c = strrchr(lastbar, '/');
		*c = '\0';
		if (strlen(lastbar) == 0) return 2; // retorna o endereco da raiz
		fname = lastbar;
	}

	// fila dos blocos a serem percorridos
	uint64_t* fila = (uint64_t*) malloc(sb->blks * sizeof(uint64_t));
	// fila que marca se um bloco foi visitado ou nao
	int* visitado = (int*) malloc(sb->blks * sizeof(int));
	int inicio = 0, fim = 0, i, aux;

	// zerando as duas filas
	for (i = 0; i < sb->blks; i++) {
		fila[i] = 0;
		visitado[i] = 0;
	}
	// colocando o inode pasta raiz no inicio da fila, marcando ela
	// como visitada e incrementando o tamanho da fila
	fila[inicio] = sb->root;
	visitado[sb->root] = 1;
	fim++;

	struct inode *in = (struct inode*) calloc(sb->blksz, 1);
	struct nodeinfo *ni = (struct nodeinfo*) calloc(sb->blksz, 1);
	while (inicio < fim) {
		// colocando o ponteiro na posicao indicada pelo inicio da fila
		lseek(sb->fd, (fila[inicio] * sb->blksz), SEEK_SET);
		// lendo os dados do inicio da fila
		aux = read(sb->fd, in, sb->blksz);
		// se o inode in for de um arquivo regular (nao eh filho)
		if (in->mode == IMREG) {
			// posicionando o ponteiro na posicao do nodeinfo
			lseek(sb->fd, ((in->meta) * sb->blksz), SEEK_SET);
			// lendo o nodeinfo
			aux = read(sb->fd, ni, sb->blksz);
			// se o nome do arquivo eh igual ao parametro procurado
			if (strcmp(ni->name, fname) == 0) {
				// libera os recursos e retorna o indice no FS
				aux = fila[inicio];
				free(fila);
				free(visitado);
				free(ni);
				free(in);
				return aux;
			}
		}
		// se o inode in for de uma pasta
		if (in->mode == IMDIR) {
			// posicionando o ponteiro na posicao do nodeinfo
			lseek(sb->fd, ((in->meta) * sb->blksz), SEEK_SET);
			// lendo o nodeinfo
			aux = read(sb->fd, ni, sb->blksz);

			// se o nome do arquivo eh igual ao parametro procurado
			if (strcmp(ni->name, fname) == 0) {
				// libera os recursos e retorna o indice no FS
				aux = fila[inicio];
				free(fila);
				free(visitado);
				free(ni);
				free(in);
				return aux;
			}

			// para cada elemento da pasta
			for (i = 0; i < NLINKS; i++) {
				// se esse elemento nao foi visitado
				if (visitado[in->links[i]] == 0) {
					if (in->links[i] != 0) {

						// marca ele como visitado
						visitado[in->links[i]] = 1;
						// insere ele no final da fila
						fila[fim] = in->links[i];
						// incrementa o final da fila
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
	// caso erro, retorna -1
	return 0;
}

/*
Adiciona um bloco a um inode no FS
*/
int linkaBlocos(struct superblock *sb, struct inode *in, uint64_t in_n, uint64_t block) {
	int i;
	uint64_t aux, iaux_n, n;
	struct inode *iaux = (struct inode*) calloc(sb->blksz, 1);

	if (in->next == 0) {
		// percorre para axar um local vazio
		for (i = 0; i < NLINKS; i++) {
			if (in->links[i] == 0) {
				in->links[i] = block;
				free(iaux);
				return 0;
			}
		}

		// cria um novo inode
		n = fs_get_block(sb);
		if (n == (uint64_t)-1) {
			free(iaux);
			return -1;
		}
		in->next = n;
		iaux->mode = IMCHILD;
		iaux->parent = in_n;
		iaux->next = 0;
		iaux->meta = in_n;
		iaux->links[0] = block;

		// escreve o novo inode
		lseek(sb->fd, n * sb->blksz, SEEK_SET);
		aux = write(sb->fd, iaux, sb->blksz);
		free(iaux);
		if (aux == -1) return -1;
		return 0;
	}

	while (in->next != 0) {
		iaux_n = in->next;
		lseek(sb->fd, iaux_n * sb->blksz, SEEK_SET);
		aux = read(sb->fd, iaux, sb->blksz);
		in = iaux;
	}

	// percorre para axar um local vazio
	for (i = 0; i < NLINKS; i++) {
		if (in->links[i] == 0) {
			in->links[i] = block;
			// escreve o inode de volta
			lseek(sb->fd, iaux_n * sb->blksz, SEEK_SET);
			aux = write(sb->fd, iaux, sb->blksz);
			free(iaux);
			return 0;
		}
	}

	// cria um novo inode
	n = fs_get_block(sb);
	if (n == (uint64_t)-1) {
		free(iaux);
		return -1;
	}
	in->next = n;
	iaux->mode = IMCHILD;
	iaux->parent = in_n;
	iaux->next = 0;
	iaux->meta = iaux_n;
	iaux->links[0] = block;

	// escreve o novo inode
	lseek(sb->fd, n * sb->blksz, SEEK_SET);
	aux = write(sb->fd, iaux, sb->blksz);

	free(iaux);
	if (aux == -1) return -1;
	return 0;
}



/*
Constroi um novo sistema de arquivos no arquivo de nome fname
*/
struct superblock * fs_format(const char *fname, uint64_t blocksize){
	NLINKS = (blocksize - (4 * sizeof(uint64_t)))/sizeof(uint64_t);

	//verifica se o tamanho do bloco eh maior que o minimo
	if(blocksize < MIN_BLOCK_SIZE){
		errno = EINVAL;
		return NULL;
	}

	//calcula o tamanho de fname.
	FILE* arquivo = fopen(fname, "r");
	long fsize;
	if(arquivo != NULL){
		fseek(arquivo, 0, SEEK_END);
		fsize = ftell(arquivo);
	}
	fclose(arquivo);

	//calcula numero de blocos
	int numeroBlocos = fsize / blocksize;

	//verifica se o numero de blocos eh maior que o minimo
	if(numeroBlocos < MIN_BLOCK_COUNT){
		errno = ENOSPC;
		return NULL;
	}

	//criando o superbloco
	struct superblock* superBloco = (struct superblock*) malloc (blocksize);
	superBloco->magic = 0xdcc605f5; //conforme estabelecido em fs.h
	superBloco->blks = numeroBlocos;
	superBloco->blksz = blocksize;

	//superbloco, nodeinfo, root e inode de root ocupam 3 blocos
	int memoriaOcupada = 3;

	//blocos livres
	superBloco->freeblks = numeroBlocos-memoriaOcupada;

	//apontador para o primeiro bloco livre
	superBloco->freelist = 3;

	//apontador para o inode da pasta raiz
	superBloco->root = 2;

	//descritor de arquivos
	superBloco->fd = open(fname, O_RDWR, S_IWRITE | S_IREAD);
	if(superBloco->fd == -1){
		errno = EBADF;
		free(superBloco);
		return NULL;
	}

	//inicializando o superbloco
	int aux = write(superBloco->fd, superBloco, superBloco->blksz);
	if(aux == -1){
		close(superBloco->fd);
		free(superBloco);
		return NULL;
	}

	//inicializando a pasta raiz
	struct nodeinfo* rootInfo = (struct nodeinfo*) calloc (superBloco->blksz,1);
	rootInfo->size = 0;
	strcpy(rootInfo->name, "/\0");
	aux = write(superBloco->fd, rootInfo, superBloco->blksz);
	free(rootInfo);

	struct inode* rootInode = (struct inode*) calloc (superBloco->blksz,1);
	rootInode->mode = IMDIR;
	rootInode->parent = 0;
	rootInode->meta = 1;
	rootInode->next = 0;
	aux = write(superBloco->fd, rootInode, superBloco->blksz);
	free(rootInode);

	//inicializando lista de blocos vazios
	struct freepage* root_fp = (struct freepage*) malloc (sizeof(struct freepage*));
	for(int i = superBloco->freelist; i < superBloco->blks; i++){
		if(i == (superBloco->blks - 1)){
			root_fp->next = 0; //ultima pagina livre
		}
		else{
			root_fp->next = i+1;
		}

		aux = write(superBloco->fd, root_fp, superBloco->blksz);
	}
	free(root_fp);

	return superBloco;
}

/*
Abre o sistema de arquivos em fname e retorna seu superbloco
*/
struct superblock * fs_open(const char *fname){
	//pega o descritor de arquivo do FS
	int descritorArquivos = open(fname, O_RDWR);

	// aplica uma trava exclusiva no arquivo (LOCK_EX = exclusive lock)
	// apenas um processo poderá usar esse arquivo de cada vez
	// flock retorna 0 se sucesso, -1 se erro
	if((flock(descritorArquivos, LOCK_EX | LOCK_NB)) == -1){
		errno = EBUSY;
		close(descritorArquivos);
		return NULL;
	}

	//move o leitor para a posição inicial do arquivo.
	lseek(descritorArquivos, 0, SEEK_SET);

	//carrega o superbloco do FS
	struct superblock* superbloco = (struct superblock*) malloc(sizeof(struct superblock));
	if(read(descritorArquivos, superbloco, sizeof(struct superblock)) == -1 ){
		close(descritorArquivos);
		free(superbloco);
		return NULL;
	}


	//verifica o erro EBADF
	if(superbloco->magic != 0xdcc605f5){
		//LOCK_UN: remove a trava do arquivo
		flock(descritorArquivos, LOCK_UN | LOCK_NB);
		close(descritorArquivos);
		errno = EBADF;
		free(superbloco);
		return NULL;
	}

	return superbloco;
}

/*
Fecha o sistema de arquivos apontado por sb
 */
int fs_close(struct superblock *sb){
	//verifica o descritor do sistema de arquivos
	if(sb->magic != 0xdcc605f5){
		errno = EBADF;
		return -1;
	}

	//LOCK_UN: remove a trava do arquivo
	if(flock(sb->fd, LOCK_UN | LOCK_NB) == -1){
		errno = EBUSY;
		return -1;
	}

	//fechando o arquivo
	int aux = close(sb->fd);
	if(aux == -1) return -1;
	free(sb);

	return 0;
}

/*
Pega um ponteiro para um bloco livre no sistema de arquivos sb
*/
uint64_t fs_get_block(struct superblock *sb){
	//verifica o descritor do sistema de arquivos
	if(sb->magic != 0xdcc605f5){
		errno = EBADF;
		return (uint64_t) 0;
	}

	//verifica se ha blocos livres
	if(sb->freeblks == 0){
		errno = ENOSPC;
		return (uint64_t) 0;
	}

	struct freepage *pagina = (struct freepage*) calloc (sb->blksz,1);
	//localizando posição do primeiro bloco livre
	lseek(sb->fd, (sb->freelist * sb->blksz), SEEK_SET);
	//verificando se há algum erro na leitura
	int aux = read(sb->fd, pagina, sb->blksz);
	if(aux == -1){
		free(pagina);
		return (uint64_t) 0;
	}

	//pegando o "ponteiro" do bloco a ser retornado
	uint64_t bloco = sb->freelist;
	//mudando o ponteiro de lista vazia para o próximo bloco livre
	sb->freelist = pagina->next;
	//decrementando a quantidade de blocos livres
	sb->freeblks--;

	//escrevendo os novos dados do super bloco (freelist e freeblks)
	lseek(sb->fd, 0, SEEK_SET);
	aux = write(sb->fd, sb, sb->blksz);
	if(aux == -1){
		free(pagina);
		return (uint64_t) 0;
	}

	free(pagina);
	return bloco;
}

/*
Retorna o bloco de numero block para a lista de blocos livres do sistema de arquivo sb
*/
int fs_put_block(struct superblock *sb, uint64_t block){
	//verifica o descritor do sistema de arquivos
	if(sb->magic != 0xdcc605f5){
		errno = EBADF;
		return -1;
	}

	//novo bloco a ser inserido na lista de blocos livres
	struct freepage *novoBloco = (struct freepage*) malloc (sb->blksz);

	//setando os ponteiros do bloco, a nova posição da tabela de blocos livres
	novoBloco->next = sb->freelist;
	sb->freelist = block;
	sb->freeblks++;

	//escrevendo os novos dados do super bloco (freelist e freeblks)
	lseek(sb->fd, 0, SEEK_SET);
	int aux = write(sb->fd, sb, sb->blksz);
	if(aux == -1){
		free(novoBloco);
		return -1;
	}

	//escrevendo novoBloco no arquivo
	lseek(sb->fd, block * sb->blksz, SEEK_SET);
	aux = write(sb->fd, novoBloco, sb->blksz);

	free(novoBloco);
	if(aux == -1) return -1;
	return 0;
}

/*
Escreve cnt bytes de buf no sistema de arquivos apontado por sb
*/
int fs_write_file(struct superblock *sb, const char *fname, char *buf, size_t cnt){
	int i, aux;
	//verifica o descritor do sistema de arquivos
	if(sb->magic != 0xdcc605f5){
		errno = EBADF;
		return -1;
	}

	//verifica se o nome do arquivo (caminho) é maior que o permitido
	if(strlen(fname) > ((sb->blksz) - (8*sizeof(uint64_t)))){
		errno = ENAMETOOLONG;
		return -1;
	}

	uint64_t arquivoN, node_atual;
	uint64_t diretorioPai_n = encontraBloco(sb,fname, 1);
	if(diretorioPai_n == 0) return -1; //por erro
	struct inode *diretorioPai = (struct inode*) calloc(sb->blksz,1);
	struct inode *arquivo = (struct inode*) calloc(sb->blksz,1);
	struct inode *aux_inode = (struct inode*) calloc(sb->blksz,1);
	struct nodeinfo *arquivoIn = (struct nodeinfo*) calloc(sb->blksz,1);
	struct nodeinfo *paiIn = (struct nodeinfo*) calloc(sb->blksz,1);

	//verifica se o arquivo existe no FS
	uint64_t arquivoAntigoN = encontraBloco(sb, fname, 0);
	if(arquivoAntigoN > 0){
		if(fs_unlink(sb,fname) == -1){
			free(diretorioPai);
			free(arquivo);
			free(aux_inode);
			free(arquivoIn);
			free(paiIn);
			return -1;
		}
		arquivoN = fs_get_block(sb);
		if(arquivoN == (uint64_t)-1){
			free(diretorioPai);
			free(arquivo);
			free(aux_inode);
			free(arquivoIn);
			free(paiIn);
			errno = ENOSPC;
			return -1;
		}

		//procura pela referencia do arq no diretorio e atualiza para o novo arq
		node_atual = diretorioPai_n;
		do{
			lseek(sb->fd, node_atual*sb->blksz, SEEK_SET);
			aux = read(sb->fd, aux_inode, sb->blksz);
			for(i=0; i<NLINKS; i++){
				if(aux_inode->links[i] == arquivoAntigoN){
					aux_inode->links[i] = arquivoN;
					lseek(sb->fd, node_atual*sb->blksz, SEEK_SET);
					aux = write(sb->fd, aux_inode, sb->blksz);
					break;
				}
			}
			node_atual = aux_inode->next;
		}
		while(node_atual != 0);
	}
	//se o arq nao existia
	else{
		//pega um novo bloco
		arquivoN = fs_get_block(sb);
		if(arquivoN == (uint64_t)-1){
			free(diretorioPai);
			free(arquivo);
			free(aux_inode);
			free(arquivoIn);
			free(paiIn);
			errno = ENOSPC;
			return -1;
		}

		//le o nodeinfo e inode do dir pai e atualiza o nodeinfo
		lseek(sb->fd, diretorioPai_n*sb->blksz, SEEK_SET);
		aux = read(sb->fd, diretorioPai, sb->blksz);
		lseek(sb->fd, diretorioPai->meta*sb->blksz, SEEK_SET);
		aux = read(sb->fd, paiIn, sb->blksz);
		paiIn->size++;
		lseek(sb->fd, diretorioPai->meta*sb->blksz, SEEK_SET);
		aux = write(sb->fd,paiIn,sb->blksz);

		//linka o novo bloco no dir pai
		linkaBlocos(sb,diretorioPai,diretorioPai_n,arquivoN);

		//escreve o inode do pai
		lseek(sb->fd, diretorioPai_n*sb->blksz, SEEK_SET);
		aux = write(sb->fd,diretorioPai,sb->blksz);
	}

	//cria estrutura do novo arq
	arquivo->parent = diretorioPai_n;
	arquivo->mode = IMREG;
	arquivo->next = 0;

	//pega novo bloco pro meta do arq
	arquivo->meta = fs_get_block(sb);
	if(arquivo->meta == (uint64_t)-1){
		free(diretorioPai);
		free(arquivo);
		free(aux_inode);
		free(arquivoIn);
		free(paiIn);
		errno = ENOSPC;
		return -1;
	}

	//cria estrutura do meta do arq e a escreve
	strcpy(arquivoIn->name,fname);
	arquivoIn->size = cnt;
	lseek(sb->fd, arquivo->meta*sb->blksz, SEEK_SET);
	aux = write(sb->fd,arquivoIn,sb->blksz);

	//cria blocos e escreve o dado
	memset(aux_inode,0,sb->blksz);
	uint64_t block_n;
	uint64_t bytes_left = (uint64_t) cnt*sizeof(char);
	void *block = calloc(sb->blksz,1);

	uint64_t last_n;
	node_atual = arquivoN;
	free(aux_inode);
	aux_inode = arquivo;
	int flageof = 0;
	do{
		for(i = 0; i<NLINKS && !flageof; i++){
			if(aux_inode->links[i] == 0){
				//limpa o bloco
				memset(block,0,sb->blksz);
				if(bytes_left >= sb->blksz){
					memcpy(block,buf,sb->blksz);
					bytes_left -= sb->blksz;
				}
				else{
					memcpy(block,buf, bytes_left);
					flageof = 1;
				}
				//pega um indice pro bloco
				block_n = fs_get_block(sb);
				if(block_n == (uint64_t) -1){
					free(diretorioPai);
					free(arquivo);
					free(arquivoIn);
					free(paiIn);
					errno = ENOSPC;
					return -1;
				}

				//linka o bloco
				aux_inode->links[i] = block_n;

				//escreve o bloco
				lseek(sb->fd, block_n*sb->blksz, SEEK_SET);
				aux = write(sb->fd,block,sb->blksz);
			}
		}
		if(flageof){
			//Escreve o inode corrente
			lseek(sb->fd, node_atual*sb->blksz, SEEK_SET);
			aux = write(sb->fd,aux_inode,sb->blksz);
		}
		else{
			//Inode cheio, e eof n encontrado

			last_n = node_atual;
			aux_inode->next = fs_get_block(sb);
			if(aux_inode->next == (uint64_t) -1){
				free(diretorioPai);
				free(arquivo);
				free(arquivoIn);
				free(paiIn);
				errno = ENOSPC;
				return -1;
			}

			//Escreve o inode corrente
			lseek(sb->fd, node_atual*sb->blksz, SEEK_SET);
			aux = write(sb->fd,aux_inode,sb->blksz);

			//Limpa o struct aux_inode e atualiza node_atual
			node_atual = aux_inode->next;
			memset(aux_inode,0,sb->blksz);

			//Cria estrutura do inode filho
			aux_inode->parent = arquivoN;
			aux_inode->meta = last_n;
			aux_inode->mode = IMCHILD;
			aux_inode->next = 0;
		}
	}
	while(!flageof);

	free(diretorioPai);
	free(arquivo);
	free(arquivoIn);
	free(paiIn);

	return 0;
}

ssize_t fs_read_file(struct superblock *sb, const char *fname, char *buf, size_t bufsz) {
    // Verifica o descritor do sistema de arquivos.
    if (sb->magic != 0xdcc605f5) {
        errno = EBADF;
        return -1;
    }

    // Verifica se o nome do arquivo (caminho) é maior que o permitido.
    if (strlen(fname) > ((sb->blksz) - (8 * sizeof(uint64_t)))) {
        errno = ENAMETOOLONG;
        return -1;
    }

    // Verifica se o arquivo existe no sistema de arquivos e salva seu "endereço".
    uint64_t block = encontraBloco(sb, fname, 0);
    if (block == 0) {
        errno = ENOENT;
        return -1;
    }

    struct inode *inode = (struct inode*) calloc(sb->blksz, 1);
    struct nodeinfo *node_info = (struct nodeinfo*) calloc(sb->blksz, 1);
    int nlinks, i;
    size_t bufaux = 0;
    char* leitor = (char*) malloc(sb->blksz);

    // Posiciona o ponteiro de arquivo na posição do inode do arquivo que será lido.
    lseek(sb->fd, block * sb->blksz, SEEK_SET);
    // Carrega o inode.
    read(sb->fd, inode, sb->blksz);

    // Verifica se o arquivo não é um diretório.
    if (inode->mode == IMDIR) {
        errno = EISDIR;
        goto cleanup;
    }

    // Posiciona o ponteiro de arquivo na posição do nodeinfo desse arquivo.
    lseek(sb->fd, inode->meta * sb->blksz, SEEK_SET);
    // Carrega o nodeinfo.
    read(sb->fd, node_info, sb->blksz);
    // Quantos links existem em um inode completo.
    nlinks = (sb->blksz - 4 * sizeof(uint64_t)) / sizeof(uint64_t);

    // Enquanto houver mais inodes e não ultrapassar o tamanho do buffer.
    while (inode->next > 0 && bufaux < bufsz) {
        // Para todos os links do inode, se não ultrapassar o tamanho do buffer.
        for (i = 0; i < nlinks && bufaux < bufsz; i++) {
            // Posiciona no link[i].
            lseek(sb->fd, inode->links[i] * sb->blksz, SEEK_SET);
            // Lê o link[i] em uma variável auxiliar chamada "leitor".
            ssize_t bytes_read = read(sb->fd, leitor, sb->blksz);
            // Concatena "leitor" com "buf".
            strncat(buf, leitor, bufsz - bufaux);
            // Atualiza a quantidade de bytes lidos.
            bufaux += bytes_read;
        }
        // Posiciona e lê o próximo inode.
        lseek(sb->fd, inode->next * sb->blksz, SEEK_SET);
        read(sb->fd, inode, sb->blksz);
    }

    // No último inode.
    int nlinks_ult_node = ((node_info->size % sb->blksz) / sizeof(uint64_t)) - 4;
    for (i = 0; i < nlinks_ult_node && bufaux < bufsz; i++) {
        size_t read_size;
        // Se o tamanho do buffer for múltiplo de sb->blksz.
        if (bufsz % sb->blksz == 0) {
            read_size = sb->blksz;
        } else { // Se o tamanho do buffer não for múltiplo de sb->blksz.
            // Calcula o módulo entre o tamanho do buffer e sb->blksz.
            size_t mod = bufsz % sb->blksz;
            read_size = mod;
        }

        // Posiciona no link[i].
        lseek(sb->fd, inode->links[i] * sb->blksz, SEEK_SET);
        // Lê o link[i] em uma variável auxiliar chamada "leitor".
        ssize_t bytes_read = read(sb->fd, leitor, read_size);
        // Concatena "leitor" com "buf".
        strncat(buf, leitor, bufsz - bufaux);
        // Atualiza a quantidade de bytes lidos.
        bufaux += bytes_read;
    }

    free(inode);
    free(node_info);
    free(leitor);
    return bufaux;

cleanup:
    // Em caso de erro, libera a memória alocada.
    free(inode);
    free(node_info);
    free(leitor);
    return -1;
}

int fs_unlink(struct superblock *sb, const char *fname) {
    // Verifica se o descritor do sistema de arquivos é válido.
    if (sb->magic != 0xdcc605f5) {
        errno = EBADF; // Define o erro como "descritor de arquivo inválido".
        return -1;
    }

    // Verifica se o nome do arquivo (caminho) é maior do que o permitido.
    if (strlen(fname) > ((sb->blksz) - (8 * sizeof(uint64_t)))) {
        errno = ENAMETOOLONG; // Define o erro como "nome de arquivo muito longo".
        return -1;
    }

    // Verifica se o arquivo existe no sistema de arquivos.
    uint64_t block = encontraBloco(sb, fname, 0);
    if (block == 0) {
        errno = ENOENT; // Define o erro como "arquivo não encontrado".
        return -1;
    }

    int aux;
    int i, index;
    struct inode *inode_atual = (struct inode*) calloc(sb->blksz, 1);
    struct inode *prox_inode = (struct inode*) calloc(sb->blksz, 1);
    struct inode *parent_dir = (struct inode*) calloc(sb->blksz, 1);
    struct nodeinfo *parent_inode = (struct nodeinfo*) calloc(sb->blksz, 1);
    struct nodeinfo *node_info = (struct nodeinfo*) calloc(sb->blksz, 1);

    // Posiciona o ponteiro na posição do primeiro inode para leitura.
    lseek(sb->fd, block * sb->blksz, SEEK_SET);
    aux = read(sb->fd, inode_atual, sb->blksz);

    // Verifica se é um diretório.
    if (inode_atual->mode == IMDIR) {
        errno = EISDIR; // Define o erro como "é um diretório".
        goto cleanup;
    }

    // Lê o inode do diretório pai.
    lseek(sb->fd, inode_atual->parent * sb->blksz, SEEK_SET);
    aux = read(sb->fd, parent_dir, sb->blksz);

    // Lê o nodeinfo do diretório pai e atualiza-o.
    lseek(sb->fd, parent_dir->meta * sb->blksz, SEEK_SET);
    aux = read(sb->fd, parent_inode, sb->blksz);
    parent_inode->size--;

    lseek(sb->fd, parent_dir->meta * sb->blksz, SEEK_SET);
    aux = write(sb->fd, parent_inode, sb->blksz);

    uint64_t node_atual;
    struct inode *aux_inode = (struct inode*) calloc(sb->blksz, 1);

    // Procura a referência do arquivo no diretório pai e remove-a.
    node_atual = inode_atual->parent;
    do {
        lseek(sb->fd, node_atual * sb->blksz, SEEK_SET);
        aux = read(sb->fd, aux_inode, sb->blksz);
        for (i = 0; i < NLINKS; i++) {
            if (aux_inode->links[i] == block) {
                aux_inode->links[i] = 0;
                lseek(sb->fd, node_atual * sb->blksz, SEEK_SET);
                aux = write(sb->fd, aux_inode, sb->blksz);
                break;
            }
        }
        node_atual = aux_inode->next;
    } while (node_atual != 0);

    free(aux_inode);
    free(parent_dir);
    free(parent_inode);

    // Lê o nodeinfo para obter o tamanho do arquivo.
    lseek(sb->fd, inode_atual->meta * sb->blksz, SEEK_SET);
    aux = read(sb->fd, node_info, sb->blksz);

    // Libera o nodeinfo desse arquivo.
    fs_put_block(sb, inode_atual->meta);

    // Libera os links utilizados.
    for (i = 0; i < NLINKS; i++) {
        if (inode_atual->links[i] > 0) {
            fs_put_block(sb, inode_atual->links[i]);
        }
    }

    // Se houver um child.
    while (inode_atual->next > 0) {
        // Salva o índice do próximo inode.
        index = inode_atual->next;
        // Lê o próximo inode.
        lseek(sb->fd, inode_atual->next * sb->blksz, SEEK_SET);
        aux = read(sb->fd, inode_atual, sb->blksz);

        // Libera os links utilizados.
        for (i = 0; i < NLINKS; i++) {
            if (inode_atual->links[i] > 0) {
                fs_put_block(sb, inode_atual->links[i]);
            }
        }

        // Libera o inode.
        fs_put_block(sb, index);
    }

    // Libera o inode deste arquivo.
    fs_put_block(sb, block);

    // Em caso de sucesso.
    free(inode_atual);
    free(prox_inode);
    free(node_info);
    return 0;

cleanup:
    // Em caso de erro, libera a memória alocada.
    free(inode_atual);
    free(prox_inode);
    free(parent_dir);
    free(parent_inode);
    free(node_info);
    return -1;
}

int fs_mkdir(struct superblock *sb, const char *dname) {
    // Verifica o descritor do sistema de arquivos.
    if (sb->magic != 0xdcc605f5) {
        errno = EBADF;  // Definir o erro EBADF
        return -1;
    }

    // Verifica se o nome do diretório é maior que o permitido.
    if (strlen(dname) > sb->blksz - 8 * sizeof(uint64_t)) {
        errno = ENAMETOOLONG;  // Definir o erro ENAMETOOLONG
        return -1;
    }

    // Verifica se o diretório pai existe e se o nome do diretório não contém espaços em branco.
    if (*dname != '/' || strchr(dname, ' ') != NULL) {
        errno = ENOENT;  // Definir o erro ENOENT
        return -1;
    }

    // Verifica se o diretório já existe.
    uint64_t superbloco = encontraBloco(sb, dname, 0);
    if (superbloco > 0) {
        errno = EEXIST;  // Definir o erro EEXIST
        return -1;
    }

    // Encontra o número do bloco do diretório pai.
    uint64_t parent_node = encontraBloco(sb, dname, 1);
    if (parent_node == 0) {
        errno = ENOENT;  // Definir o erro ENOENT
        return -1;
    }

    // Obtém blocos para o novo diretório e as informações do nó.
    uint64_t dir_node = fs_get_block(sb);
    uint64_t dir_node_info_number = fs_get_block(sb);
    if (dir_node_info_number == (uint64_t)-1 || dir_node == (uint64_t)-1) {
        return -1;  // Falha na obtenção de blocos
    }

    // Aloca memória para os diretórios pai, novo diretório e informações do nó.
    struct inode *parent_dir = (struct inode*) calloc(sb->blksz, 1);
    struct inode *dir = (struct inode*) calloc(sb->blksz, 1);
    struct nodeinfo *dir_node_info = (struct nodeinfo*) calloc(sb->blksz, 1);
    struct nodeinfo *parent_node_info = (struct nodeinfo*) calloc(sb->blksz, 1);

    // Configura as informações do novo diretório.
    dir->mode = IMDIR;
    dir->next = 0;
    dir->parent = parent_node;
    dir->meta = dir_node_info_number;

    // Extrai o nome do diretório do caminho fornecido.
    char *auxc = strrchr(dname, '/');
    strcpy(dir_node_info->name, auxc);
    dir_node_info->size = 0;

    // Lê o diretório pai do disco.
    lseek(sb->fd, parent_node* sb->blksz, SEEK_SET);
    read(sb->fd, parent_dir, sb->blksz);

    // Linka o novo diretório ao diretório pai.
    linkaBlocos(sb, parent_dir, parent_node, dir_node);

    // Lê as informações do nó do diretório pai para atualizar o número de arquivos.
    lseek(sb->fd, parent_dir->meta* sb->blksz, SEEK_SET);
    read(sb->fd, parent_node_info, sb->blksz);
    parent_node_info->size++;

    lseek(sb->fd, parent_dir->meta* sb->blksz, SEEK_SET);
    write(sb->fd, parent_node_info, sb->blksz);

    // Escreve o diretório pai de volta no disco.
    lseek(sb->fd, parent_node* sb->blksz, SEEK_SET);
    write(sb->fd, parent_dir, sb->blksz);

    // Escreve o novo diretório e as informações do nó no disco.
    lseek(sb->fd, dir_node* sb->blksz, SEEK_SET);
    write(sb->fd, dir, sb->blksz);
    lseek(sb->fd, dir_node_info_number* sb->blksz, SEEK_SET);
    write(sb->fd, dir_node_info, sb->blksz);

    // Libera a memória alocada.
    free(parent_dir);
    free(parent_node_info);
    free(dir);
    free(dir_node_info);
    return 0;
}

int fs_rmdir(struct superblock *sb, const char *dname) {
	// Verifica se o descritor do sistema de arquivos é válido.
	if (sb->magic != 0xdcc605f5) {
		errno = EBADF;
		return -1;
	}

	// Verifica se o nome do diretório (caminho) excede o tamanho máximo permitido.
	if (strlen(dname) > (sb->blksz - 8 * sizeof(uint64_t))) {
		errno = ENAMETOOLONG;
		return -1;
	}

	// Verifica se o caminho do diretório não começa com '/' ou contém espaços em branco.
	if ((*dname != '/') || (strchr(dname, ' ') != NULL)) {
		errno = ENOENT;
		return -1;
	}

	// Verifica se o diretório a ser removido existe.
	uint64_t block = encontraBloco(sb, dname, 0);
	if (block == 0) {
		errno = ENOENT;
		return -1;
	}

	uint64_t parent_node = encontraBloco(sb, dname, 1);
	uint64_t node_atual;

	struct inode *parent_dir = (struct inode*) calloc(sb->blksz, 1);
	struct inode *dir = (struct inode*) calloc(sb->blksz, 1);
	struct nodeinfo *dir_node_info = (struct nodeinfo*) calloc(sb->blksz, 1);
	struct nodeinfo *parent_node_info = (struct nodeinfo*) calloc(sb->blksz, 1);

	// Lê o inode do diretório.
	lseek(sb->fd, block * sb->blksz, SEEK_SET);
	read(sb->fd, dir, sb->blksz);

	// Lê as informações do nó do diretório.
	lseek(sb->fd, dir->meta * sb->blksz, SEEK_SET);
	read(sb->fd, dir_node_info, sb->blksz);

	// Verifica se o diretório não está vazio.
	if (dir_node_info->size > 0) {
		errno = ENOTEMPTY;
		goto cleanup;
	}

	fs_put_block(sb, dir->meta); // Deleta o nó de informações do diretório.
	fs_put_block(sb, block);     // Deleta o inode do diretório.
	memset(dir, 0, sb->blksz);   // Limpa a estrutura do diretório.

	// Atualiza as informações do nó de informações e do inode do diretório pai.
	lseek(sb->fd, parent_node* sb->blksz, SEEK_SET);
	read(sb->fd, parent_dir, sb->blksz);
	lseek(sb->fd, parent_dir->meta* sb->blksz, SEEK_SET);
	read(sb->fd, parent_node_info, sb->blksz);
	parent_node_info->size--;

	lseek(sb->fd, parent_dir->meta* sb->blksz, SEEK_SET);
	write(sb->fd, parent_node_info, sb->blksz);

	// Procura pela referência ao diretório a ser removido no diretório pai e a remove.
	node_atual = parent_node;
	do {
		lseek(sb->fd, node_atual* sb->blksz, SEEK_SET);
		read(sb->fd, dir, sb->blksz);

		for(int ii = 0; ii < NLINKS; ii++) {
			if (dir->links[ii] == block) {
				dir->links[ii] = 0;
				lseek(sb->fd, node_atual* sb->blksz, SEEK_SET);
				write(sb->fd, dir, sb->blksz);

				break;
			}
		}
		node_atual = dir->next;
	} while (node_atual != 0);

cleanup:
	free(parent_dir);
	free(parent_node_info);
	free(dir);
	free(dir_node_info);
	return 0;
}

#include <stdlib.h>
#include <string.h>

char *fs_list_dir(struct superblock *sb, const char *dname) {
    // Verifica o descritor do sistema de arquivos.
    if (sb->magic != 0xdcc605f5) {
        errno = EBADF;
        return NULL;
    }

    // Verifica se o nome do arquivo (caminho) é maior que o permitido.
    if (strlen(dname) > (sb->blksz - 8 * sizeof(uint64_t))) {
        errno = ENAMETOOLONG;
        return NULL;
    }

    // Procura o índice do inode de dname e verifica se dname existe.
    uint64_t superbloco = encontraBloco(sb, dname, 0);
    if (superbloco == 0) {
        errno = ENOENT;
        return NULL;
    }

    int i;
    char *ret = (char*) calloc(500, sizeof(char));
    struct inode *inode = (struct inode*) calloc(1, sb->blksz);
    struct inode *inode_aux = (struct inode*) calloc(1, sb->blksz);
    struct nodeinfo *node_info = (struct nodeinfo*) calloc(1, sb->blksz);
    struct nodeinfo *node_info_aux = (struct nodeinfo*) calloc(1, sb->blksz);

    char *tok;
    char nome[50];

    // Posiciona e lê o inode de dname.
    lseek(sb->fd, superbloco * sb->blksz, SEEK_SET);
    read(sb->fd, node_info, sb->blksz);

    // Verifica se o caminho dname aponta para um diretório.
    if (inode->mode != IMDIR) {
        goto cleanup;
    }

    // Posiciona e lê o nodeinfo do diretório dname.
    lseek(sb->fd, inode->meta * sb->blksz, SEEK_SET);
    read(sb->fd, node_info, sb->blksz);

    // Percorre os links do diretório dname.
    for (i = 0; i < NLINKS; i++) {
        if (inode->links[i] != 0) {
            // Lê o inode de cada arquivo/pasta dentro do diretório dname.
            lseek(sb->fd, inode->links[i] * sb->blksz, SEEK_SET);
            read(sb->fd, inode_aux, sb->blksz);

            // Lê o nodeinfo desse inode.
            lseek(sb->fd, inode_aux->meta * sb->blksz, SEEK_SET);
            read(sb->fd, node_info_aux, sb->blksz);

            // Pega o nome completo desse arquivo/pasta e divide-o em
            // substrings divididas pelo caractere '/'. Salva a última parte.
            tok = strtok(node_info_aux->name, "/");
            while (tok != NULL){
                strcpy(nome, tok);
                tok = strtok(NULL, "/");
            }

            if (inode_aux->mode == IMDIR)
                strcat(nome, "/");

            // Concatena o nome do arquivo/pasta à string de resultado.
            strcat(ret, nome);

            // Acrescenta um espaço entre os arquivos/pastas.
            if (i + 1 < node_info->size)
                strcat(ret, " ");
        }
    }

    cleanup:
	    free(inode);
	    free(inode_aux);
	    free(node_info);
	    free(node_info_aux);
    	return ret;
}
