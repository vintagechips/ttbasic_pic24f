/*
	Title: TOYOSHIKI TinyBASIC
	Filename: ttbasic/basic.c
	(C)2012 Tetsuya Suzuki, All rights reserved.
*/

//コンパイラに依存するヘッダ

//ハードウェア(OSのAPI)に依存するヘッダ
#include <stdlib.h> // rnd
#include "led.h"
#include "sw.h"
#include "sci.h"//c_putch,c_getch,c_kbhit
#include "flash.h"

// TinyBASIC symbols
#define SIZE_LINE 80 //1行入力文字数と終端記号のサイズ
#define SIZE_IBUF 80//中間コードバッファのサイズ
#define SIZE_LIST 1023//リスト保存領域のサイズ
#define SIZE_ARAY 128//配列領域のサイズ
#define SIZE_GSTK 6//GOSUB用スタックサイズ(2単位)
#define SIZE_LSTK 15//FOR用スタックサイズ(5単位)

// Depending on device functions
// TO-DO Rewrite these functions to fit your machine
#define STR_EDITION "PIC24F"

// Terminal control
#define c_putch(c) putch2(c)
#define c_getch() getch2()
#define c_kbhit() kbhit2()

#define KEY_ENTER 13
void newline(void){c_putch(13);c_putch(10);}

//乱数を返す
//1から指定値まで
short getrnd(short value){
	//ハードウェアに依存
	return (rand() % value) + 1;
    //return 1;
}

char lbuf[SIZE_LINE];//コマンドラインのバッファ
unsigned char ibuf[SIZE_IBUF];//中間コードバッファ
short var[26];// 変数領域
short ary[SIZE_ARAY];//配列領域
unsigned char listbuf[SIZE_LIST + 1];//リスト保存領域+ブートフラグ
unsigned char* clp;//カレント行の先頭を指すポインタ
unsigned char *cip;//パースの中間コードのポインタ

unsigned char* gstk[SIZE_GSTK];//スタック領域
unsigned char gstki;//スタックインデクス
unsigned char* lstk[SIZE_LSTK];//スタック領域
unsigned char lstki;//スタックインデクス

//PROTOTYPES
short iexp(void);

//intermediate code table
//__attribute__((section(".romdata"), space(prog), aligned(64)))
const char* kwtbl[] = {
	"IF",
	"GOTO",
	"GOSUB",
	"RETURN",
	"FOR",
	"TO",
	"STEP",
	"NEXT",
	"PRINT",
	"INPUT",
	"LED",//extend
	"REM",
	"LET",
	"STOP",
    "ON",
    "OFF",
	";",
	"-",
	"+",
	"*",
	"/",
	">=",
	"<>",
	">",
	"=",
	"<=",
	"<",
	"(",
	")",
	",",
	"#",
	"@",
	"RND",
	"ABS",
	"SW",//extend
	"SIZE",
	"LIST",
	"RUN",
	"NEW",
	"SAVE",//extend
	"BOOT",//extend
	"LOAD",//extend
};

enum{
	I_IF,
	I_GOTO,
	I_GOSUB,
	I_RETURN,
	I_FOR,
	I_TO,
	I_STEP,
	I_NEXT,
	I_PRINT,
	I_INPUT,
	I_LED,//extend
	I_REM,
	I_LET,
	I_STOP,
	I_ON,
	I_OFF,
	I_SEMI,//リスト表示で空白を入れるが数値は後ろに空白を入れない
	I_MINUS,//演算子の先頭
	I_PLUS,
	I_MUL,
	I_DIV,
	I_GTE,
	I_NEQ,
	I_GT,
	I_EQ,
	I_LTE,
	I_LT,
	I_OPEN,
	I_CLOSE,//演算子の末尾
	I_COMMA,//ここまでは数値と変数が後ろに空白を入れない
	I_SHARP,
	I_ARRAY,
	I_RND,
	I_ABS,
	I_SW,//extend
	I_SIZE,
	I_LIST,
	I_RUN,
	I_NEW,
	I_SAVE,//extend
	I_BOOT,//extend
	I_LOAD,//extend//ここまではキーワード(配列のループで検索)
	I_NUM,//キーワードではないので空白は個別処理される
	I_VAR,
	I_STR,
	I_EOL//中間コードの行末
};

#define SIZE_KWTBL (sizeof(kwtbl) / sizeof(char*))
#define IS_OP(p) (p >= I_MINUS && p <= I_CLOSE)
#define IS_SEP(p) (p == I_COMMA || p == I_SEMI)
#define IS_TOKSP(p) (/*p >= I_IF && */p <= I_LET && p != I_RETURN)

//gotoの後ろのエラーはチェックされない
//runはプログラムが存在すれば後ろのエラーはチェックされない
unsigned char err;//エラー番号(エラーメッセージのインデクス)
//__attribute__((section(".romdata"), space(prog), aligned(64)))
const char* errmsg[] ={
	"OK",
	"Devision by zero",
	"Overflow",
	"Subscript out of range",
	"Icode buffer full",
	"List full",
	"GOSUB too many nested",
	"RETURN stack underflow",
	"FOR too many nested",
	"NEXT without FOR",
	"NEXT without counter",
	"NEXT mismatch FOR",
	"FOR without variable",
	"FOR without TO",
	"LET without variable",
	"IF without condition",
	"Undefined line number",
	"\'(\' or \')\' expected",
	"\'=\' expected",
	"Illegal command",
	"Syntax error",
	"Internal error",
	"Abort by [ESC]"
};

enum{
	ERR_OK,
	ERR_DIVBY0,
	ERR_VOF,
	ERR_SOL,
	ERR_IBUFOF,
	ERR_LBUFOF,
	ERR_GSTKOF,
	ERR_GSTKUF,
	ERR_LSTKOF,
	ERR_LSTKUF,
	ERR_NEXTWOV,
	ERR_NEXTUM,
	ERR_FORWOV,
	ERR_FORWOTO,
	ERR_LETWOV,
	ERR_IFWOC,
	ERR_ULN,
	ERR_PAREN,
	ERR_VWOEQ,
	ERR_COM,
	ERR_SYNTAX,
	ERR_SYS,
	ERR_ESC
};

//Standard C libraly最低レベルの関数
char c_toupper(char c) {return(c <= 'z' && c >= 'a' ? c - 32 : c);}
char c_isprint(char c) {return(c >= 32  && c <= 126);}
char c_isspace(char c) {return(c == ' ' || (c <= 13 && c >= 9));}
char c_isdigit(char c) {return(c <= '9' && c >= '0');}
char c_isalpha(char c) {return ((c <= 'z' && c >= 'a') || (c <= 'Z' && c >= 'A'));}

void c_puts(const char *s) {
	while (*s) c_putch(*s++); //終端でなければ表示して繰り返す
}
void c_gets() {
	char c; //文字
	unsigned char len; //文字数

	len = 0; //文字数をクリア
	while ((c = c_getch()) != KEY_ENTER) { //改行でなければ繰り返す
		if (c == 9) c = ' '; //［Tab］キーは空白に置き換える
		//［BackSpace］キーが押された場合の処理（行頭ではないこと）
		if (((c == 8) || (c == 127)) && (len > 0)) {
			len--; //文字数を1減らす
			c_putch(8); c_putch(' '); c_putch(8); //文字を消す
		}	else
		//表示可能な文字が入力された場合の処理（バッファのサイズを超えないこと）
		if (c_isprint(c) && (len < (SIZE_LINE - 1))) {
			lbuf[len++] = c; //バッファへ入れて文字数を1増やす
			c_putch(c); //表示
		}
	}
	newline(); //改行
	lbuf[len] = 0; //終端を置く

	if (len > 0) { //もしバッファが空でなければ
		while (c_isspace(lbuf[--len])); //末尾の空白を戻る
		lbuf[++len] = 0; //終端を置く
	}
}

//指定の値を指定の桁数で表示
//指定の値が指定の桁数に満たなかった場合のみ空白で生める
void putnum(short value, short d) {
	unsigned char dig; //桁位置
	unsigned char sign; //負号の有無（値を絶対値に変換した印）

	if (value < 0) { //もし値が0未満なら
		sign = 1; //負号あり
		value = -value; //値を絶対値に変換
	}
	else {
		sign = 0; //負号なし
	}

	lbuf[6] = 0; //終端を置く
	dig = 6; //桁位置の初期値を末尾に設定
	do { //次の処理をやってみる
		lbuf[--dig] = (value % 10) + '0'; //1の位を文字に変換して保存
		value /= 10; //1桁落とす
	} while (value > 0); //値が0でなければ繰り返す

	if (sign) //もし負号ありなら
		lbuf[--dig] = '-'; //負号を保存

	while (6 - dig < d) { //指定の桁数を下回っていれば繰り返す
		c_putch(' '); //桁の不足を空白で埋める
		d--; //指定の桁数を1減らす
	}
	c_puts(&lbuf[dig]); //桁位置からバッファの文字列を表示
}

//数値のみ入力可能、戻り値は数値
//INPUTでのみ使っている
short getnum(){
	short value, tmp;
	char c;
	unsigned char len;
	unsigned char sign;

	len = 0;
	while((c = c_getch()) != 13){
		if((c == 8) && (len > 0)){//バックスペースの処理
			len--;
			c_putch(8); c_putch(' '); c_putch(8);
		} else
		if(	(len == 0 && (c == '+' || c == '-')) ||
			(len < 6 && c_isdigit(c))){//数字と符号の処理
			lbuf[len++] = c;
			c_putch(c);
		}
	}
	newline();
	lbuf[len] = 0;//文字列の終端記号を置いて入力を終了

	switch(lbuf[0]){
	case '-':
		sign = 1;
		len = 1;
		break;
	case '+':
		sign = 0;
		len = 1;
		break;
	default:
		sign = 0;
		len = 0;
		break;
	}

	value = 0;//数値を初期化
	tmp = 0;//過渡的数値を初期化
	while(lbuf[len]){
		tmp = 10 * value + lbuf[len++] - '0';
		if(value > tmp){
			err = ERR_VOF;
		}
		value = tmp;
	}
	if(sign)
		return -value;
	return value;
}

//GOSUB-RETURN用のスタック操作
void gpush(unsigned char* pd){
	if(gstki < SIZE_GSTK){
		gstk[gstki++] = pd;
		return;
	}
	err = ERR_GSTKOF;
}

unsigned char* gpop(){
	if(gstki > 0){
		return gstk[--gstki];
	}
	err = ERR_GSTKUF;
	return NULL;
}

//FOR-NEXT用のスタック操作
void lpush(unsigned char* pd){
	if(lstki < SIZE_LSTK){
		lstk[lstki++] = pd;
		return;
	}
	err = ERR_LSTKOF;
}

unsigned char* lpop(){
	if(lstki > 0){
		return lstk[--lstki];
	}
	err = ERR_LSTKUF;
	return NULL;
}

//ipが指すBNNのバイト列からNNが表すshort型の値を返す
//Bが0の場合(リストの終端)はNNが存在しないので特別な値32767を返す
//中間コードポインタは進めない、必要なら3を足す
short getvalue(unsigned char* ip){
	if(*ip == 0)
		return 32767;//行検索の対策として最大行番号を返す
	return((short)*(ip + 1) + ((short)*(ip + 2) << 8));
}

//括弧でくくられた式を値に変換して中間コードポインタを進める
short getparam(){
	short value;

	if(*cip != I_OPEN){
		err = ERR_PAREN;
		return 0;
	}
	cip++;
	value = iexp();
	if(err) return 0;

	if(*cip != I_CLOSE){
		err = ERR_PAREN;
		return 0;
	}
	cip++;

	return value;
}

//行番号を指定して行番号が等しいか大きい行の先頭のポインタを得る
//グローバルな変数を変更しない
unsigned char* getlp(short lineno){
	unsigned char *lp;

	lp = listbuf;
	while(*lp){
		if(getvalue(lp) >= lineno)
			break;
		lp += *lp;
	}
	return lp;
}

//行バッファのトークンを中間コードに変換して中間コードバッファに保存
//この中にputnumを使ってはいけない
//中間コードはインデックスlenで移動するから中間コードポインタを使わない
//空白を削除
//末尾にI_EOLを置く
//変換できたら長さを返す、変換できなかったら0を返す
//エラーフラグを操作しない
//文法チェックは変数の連続をはじくだけ
unsigned char toktoi(){
	unsigned char i;//繰り返しのカウンタ(一時的に中間コードを意味する)
	unsigned char len;//中間コードの総バイト数
	short value;//数字を数値に変換したとき使う変数
	short tmp;//数値への変換過程でオーバーフローでないことを確認する変数
	char* pkw;//キーワードを指すポインタ
	char* ptok;//キーワードと比較する文字列のトークンを指すポインタ
	char c;//文字列の囲み文字("または')を保持する
	char* s;//ラインバッファの文字列を指すポインタ

	s = lbuf;
	len = 0;//中間コードの総バイト数をリセット
	while(*s){//ソースバッファの末尾まで続ける
		while(c_isspace(*s)) s++;//空白をスキップ

		//キーワードを中間コードに変換できるかどうか調べる
		for(i = 0; i < SIZE_KWTBL; i++){
			pkw = (char *)kwtbl[i];//キーワードを指す
			ptok = s;//行バッファの先頭を指す
			//文字列の比較
			while((*pkw != 0) && (*pkw == c_toupper(*ptok))){
				pkw++;
				ptok++;
			}
			if(*pkw == 0){//中間コードに変換できた場合
				//iが中間コード
				if(len >= SIZE_IBUF - 1){//バッファに空きがない場合
					err = ERR_IBUFOF;
					return 0;
				}
				//バッファに空きがある場合
				ibuf[len++] = i;
				s = ptok;//ソースバッファの次の位置
				break;
			}
		}

		//ステートメントが数値、文字列、変数以外の引数を要求する場合の処理
		switch(i){
		case I_REM://REMの場合は以降をそのまま保存して終了
			while(c_isspace(*s)) s++;//空白をスキップ
			ptok = s;
			for(i = 0; *ptok++; i++);//行末までの長さをはかる
			if(len >= SIZE_IBUF - 2 - i){//バッファに空きがない場合
				err = ERR_IBUFOF;
				return 0;
			}

			//バッファに空きがある場合
			ibuf[len++] = i;//文字列の長さ
			while(i--){//文字列をコピー
				ibuf[len++] = *s++;
			}
			return len;
		default:
			break;
		}

		if(*pkw != 0){//キーワードではなかった場合
			ptok = s;//ポインタを先頭に再設定

			if(c_isdigit(*ptok)){//数字の変換を試みる
				value = 0;//数値を初期化
				tmp = 0;//過渡的数値を初期化
				do{
					tmp = 10 * value + *ptok++ - '0';
					if(value > tmp){
						err = ERR_VOF;
						return 0;
					}
					value = tmp;
				} while(c_isdigit(*ptok));

				if(len >= SIZE_IBUF - 3){//バッファに空きがない場合
					err = ERR_IBUFOF;
					return 0;
				}
				//バッファに空きがある場合
				s = ptok;
				ibuf[len++] = I_NUM;//数値を表す中間コード
				ibuf[len++] = value & 255;
				ibuf[len++] = value >> 8;
			} else  //数字ではなかったら

			//変数の変換を試みる
			if(c_isalpha(*ptok)){//変数だったら
				if(len >= SIZE_IBUF - 2)
				{//バッファに空きがない場合
					err = ERR_IBUFOF;
					return 0;
				}
				//バッファに空きがある場合
				if(len >= 2 && ibuf[len -2] == I_VAR){//直前が変数なら
					 err = ERR_SYNTAX;
					 return 0;
				}
				//直前が変数でなければ
				ibuf[len++] = I_VAR;//変数を表す中間コード
				ibuf[len++] = c_toupper(*ptok) - 'A';//変数のインデクス
				s++;
			} else //変数ではなかったら

			//文字列の変換を試みる
			if(*s == '\"' || *s == '\''){//文字列の開始だったら
				c = *s;
				s++;//"の次へ
				ptok = s;
				for(i = 0; (*ptok != c) && c_isprint(*ptok); i++)//文字列の長さをはかる
					ptok++;
				if(len >= SIZE_IBUF - 1 - i){//バッファに空きがない場合
					err = ERR_IBUFOF;
					return 0;
				}
				//バッファに空きがある場合
				ibuf[len++] = I_STR;//文字列を表す中間コード
				ibuf[len++] = i;//文字列の長さ
				while(i--){//文字列をコピー
					ibuf[len++] = *s++;
				}
				if(*s == c) s++;//"の次へ(NULL終端で停止している可能性がある)

			//文法違いの処理
			} else { //どれにも該当しなければ
				err = ERR_SYNTAX;
				return 0;//エラー信号を返してこの関数を打ち切り
			}
		}
	}
	ibuf[len++] = I_EOL;//行の終端記号を保存
	return len;//行の終端記号を含めたバイト数(それ自身の1バイトを含む)を返す
}

//中間コードの1行分をリスト表示
//引数は行の中の先頭の中間コードへのポインタ
void putlist(unsigned char* ip){
	unsigned char i;//繰り返しのカウンタ(一時的に中間コードを意味する)
	short value;

	while(*ip != I_EOL){//icodeバッファの末尾まで続ける
		if(*ip < SIZE_KWTBL){//キーワードの場合
			c_puts(kwtbl[*ip]);
			if(IS_TOKSP(*ip) || *ip == I_SEMI)c_putch(' ');
			if(*ip == I_REM){
				ip++;
				i = *ip++;
				while(i--){
					c_putch(*ip++);
				}
				return;
			}
			ip++;
		} else
		if(*ip == I_NUM){//数値の場合
			putnum(getvalue(ip), 0);
			ip += 3;
			if(!IS_OP(*ip) && !IS_SEP(*ip)) c_putch(' ');
		} else
		if(*ip == I_VAR){//変数の場合
			ip++;
			c_putch(*ip++ + 'A');
			if(!IS_OP(*ip) && !IS_SEP(*ip)) c_putch(' ');
		} else
		if(*ip == I_STR){//文字列の場合
			ip++;

			value = 0;//仮のフラグ
			i = *ip;//文字数を取得
			while(i--){
				if(*(ip + i + 1) == '\"')
					value = 1;
			}
			if(value) c_putch('\''); else c_putch('\"');
			i = *ip++;
			while(i--){
				c_putch(*ip++);
			}
			if(value) c_putch('\''); else c_putch('\"');
		} else {//どれにも該当しなければ
			err = ERR_SYS;//これはないと思う
			return;//エラー信号を返してこの関数を打ち切り
		}
	}
}

//グローバルな変数を変更しない
short getsize(){
	short value;
	unsigned char* lp;

	lp = listbuf;
	while(*lp){//末尾まで進める
		lp += *lp;
	}

	value = listbuf + SIZE_LIST - lp - 1;
	return value;
}

//中間コードバッファの内容をリストに保存
//エラーをエラーフラグで知らせる
void inslist(){
	unsigned char len;
	unsigned char *lp1, *lp2;

	cip = ibuf;//ipは中間コードバッファの中間コードを指す(リストの中間コードではない)
	clp = getlp(getvalue(cip));//行番号を渡して挿入行の位置を取得

	//BUG 同一行番号があってバッファオーバーフローのとき削除される
	//同一行番号が存在する場合は削除または置き換え、とりあえず削除
	lp1 = clp;
	if(getvalue(lp1) == getvalue(cip)){
		//バグ対策のための応急処置
		if((getsize() - *lp1) < *cip){//もし行を削除しても新しい行を挿入できないなら
			err = ERR_LBUFOF;
			return;
		}

		lp2 = lp1 + *lp1;//転送元を計算
		while((len = *lp2) != 0){//末尾まで続ける
			while(len--){
				*lp1++ = *lp2++;
			}
		}
		*lp1 = 0;
	}

	//行番号だけでステートメントがなければもう何もしない
	if(*cip == 4)
			return;

	//挿入可能かどうかを調べる
	while(*lp1){//末尾まで進める
		lp1 += *lp1;
	}

	if(*cip > (listbuf + SIZE_LIST - lp1 - 1)){//挿入不可能なら
		err = ERR_LBUFOF;
		return;
	}
	//空きを作る
	len = lp1 - clp + 1;
	lp2 = lp1 + *cip;
	while(len--){
		*lp2-- = *lp1--;
	}

	//挿入する
	len = *cip;//挿入する長さ
	while(len--){
		*clp++ = *cip++;
	}
}

short getabs(){
	short value;

	value = getparam();
	if(err) return 0;

	if(value < 0) value *= -1;
	return value;
}

short getarray()
{
	short index;

	index = getparam();
	if(err) return 0;

	if(index < SIZE_ARAY){
		return ary[index];
	} else {
		err = ERR_SOL;
		return 0;
	}
}

short ivalue(){
	short value;

	switch(*cip){
	 case I_PLUS:
		 cip++;
		 value = ivalue();
		 break;
	 case I_MINUS:
		 cip++;
		 value = 0 - ivalue();
		 break;
	 case I_VAR:
		 cip++;
		 value = var[*cip++];
		 break;
	 case I_NUM:
		 value = getvalue(cip);
		 cip += 3;
		 break;
	 case I_ARRAY:
		 cip++;
		value = getarray();
		break;
	 case I_OPEN:
		 value = getparam();
		break;
	 case I_RND:
		cip++;
		value = getparam();
		if (err)
			return -1;
		value = getrnd(value);
		break;
	 case I_ABS:
		cip++;
		value = getabs();
		break;
	 case I_SIZE:
		cip++;
		if(*cip == I_OPEN){
			cip++;
			if(*cip == I_CLOSE)
				cip++;
			else{
				err = ERR_PAREN;
			}
		}
		value = getsize();
		break;
	case I_SW://extend
		cip++;
		if(*cip == I_OPEN){
			cip++;
			if(*cip == I_CLOSE)
				cip++;
			else{
				err = ERR_PAREN;
			}
		}
		value = (short)swstat;
		break;

	 default:
		value = 0;
		err = ERR_SYNTAX;
		break;
	}
	return value;
}

short icalc()
{
	short value1, value2;

	value1 = ivalue();
	//if(err) return value1;//実験中

	while(1){
		if(*cip == I_DIV){
			cip++;
			value2 = ivalue();
			//if(err)	break;////実験中
			if(value2 == 0){
				err = ERR_DIVBY0;
				break;
			}
			value1 /= value2;
		} else
		if(*cip == I_MUL){
			cip++;
			value2 = ivalue();
			//if(err)	break;//実験中
			value1 *= value2;
		} else {
			break;
		}
	}
	return value1;
}

short iexp()
{
	short value1, value2;

	value1 = icalc();
	//if(err)	return value1;

	while(*cip == I_PLUS || *cip == I_MINUS){
		value2 = icalc();
		//if(err)	break;
		value1 += value2;
	}
	return value1;
}

void iprint(){
	short value;
	short len;
	unsigned char i;

	len = 0;
	while(1){
		switch(*cip){
		case I_SEMI:
		case I_EOL:
			break;
		case I_STR:
			cip++;
			i = *cip++;
			while(i--){
				c_putch(*cip++);
			}
			break;
		case I_SHARP:
			cip++;
			len = iexp();
			if(err) return;
			break;
		default:
			value = iexp();
			if(err) return;
			putnum(value, len);
			break;
		}

		if(*cip == I_COMMA){
			cip++;
		}else{
			break;
		}
	};
	newline();
}

void iinput(){
	unsigned char i;
	short value;
	short index;

	while(1){
		switch(*cip){
		case I_STR://文字列なら
			cip++;
			i = *cip++;//文字数を取得
			while(i--){//文字を表示
				c_putch(*cip++);
			}
			if(*cip == I_VAR){//文字列のすぐ後ろが変数なら
				cip++;
				value = getnum();
				//if(err) return;//実験中
				var[*cip++] = value;
			} else
			if(*cip == I_ARRAY){//文字列のすぐ後ろが配列なら
				cip++;
				index = getparam();//インデックスを取得
				if(err) return;
				if(index >= SIZE_ARAY){
					err = ERR_SOL;
					return;
				}
				value = getnum();
				//if(err) return;//実験中

				ary[index] = value;
			}
			break;
		case I_VAR:
			cip++;
			c_putch(*cip + 'A');
			c_putch(':');
			value = getnum();
			//if(err) return;//実験中
			var[*cip++] = value;
			break;
		case I_ARRAY:
			cip++;
			index = getparam();//インデックスを取得
			if(err){
				return;
			}
			if(index >= SIZE_ARAY){
				err = ERR_SOL;
				return;
			}
			c_putch('@');c_putch('(');
			putnum(index,0);
			c_putch(')');c_putch(':');
			value = getnum();
			//if(err) return;//実験中
			ary[index] = value;
			break;
		//default://どれでもなければエラー
		//	err = ERR_SYNTAX;
		//	return;
		}

		switch(*cip){
		case I_COMMA://コンマなら継続
			cip++;
			break;
		case I_SEMI://セミコロンなら繰り返しを終了
		case I_EOL://行末なら繰り返しを終了
			return;
		default://どれでもなければ行末エラー
			err = ERR_SYNTAX;
			return;
		}
	}
}

char iif(){
	short value1, value2;
	unsigned char i;
	
	value1 = iexp();
	if(err) return -1;

	i = *cip++;//演算子を取得

	value2 = iexp();
	if(err) return -1;

	switch(i){
	case I_EQ:
		return value1 == value2;
	case I_NEQ:
		return value1 != value2;
	case I_LT:
		return value1 <  value2;
	case I_LTE:
		return value1 <= value2;
	case I_GT:
		return value1 >  value2;
	case I_GTE:
		return value1 >= value2;
	default:
		 err = ERR_IFWOC;
		 return -1;
	}
}

void ivar(){
	short value;
	short index;

	index = *cip++;//変数名を取得
	if(*cip == I_EQ){
		cip++;
		value = iexp();
		if(err) return;
	} else {
		err = ERR_VWOEQ;
		return;
	}

	if(index < 26){
		var[index] = value;
	} else {
		err = ERR_SOL;
	}
}

void iarray(){
	short value;
	short index;

	index = getparam();//インデックスを取得
	if(err) return;
	if(*cip == I_EQ){
		cip++;
		value = iexp();
		if(err) return;
	} else {
		err = ERR_VWOEQ;
		return;
	}

	if(index < SIZE_ARAY){
		ary[index] = value;
	} else {
		err = ERR_SOL;
	}
}

void ilet(){
	switch(*cip){
	 case I_VAR://変数なら
		cip++;//変数名へ進める
		ivar();
		break;
	case I_ARRAY://配列なら
		cip++;
		iarray();
		break;
	 default:
		err = ERR_LETWOV;
		break;
	}
}

void iled(){
    short no;
    unsigned char sw;
    
    no = iexp();
	if(err) return;
	if(*cip == I_ON){
        cip++;
        sw = 0;
    } else
    if(*cip == I_OFF){
        cip++;
        sw = 1;
    } else {
        err = ERR_SYNTAX;
        return;
    }
    led_write(no, sw);
}

//リストを表示する
//中間コードポインタが末尾まで進む
void ilist(){
	short lineno;

	//引数を調べる
	if(*cip == I_NUM){//引数があれば
		lineno = getvalue(cip);//行番号を取得
		cip += 3;
	} else {
		lineno = 0;//なければ行番号は0
	}

	//行番号の指定があった場合はそれ未満を無視
	for(	clp = listbuf;//リストの先頭からはじめる
			*clp &&//リストの終端(先頭が0)でなくて
			(getvalue(clp) < lineno);//行番号が指定未満なら
			clp += *clp);//次の行へ

	while(*clp){//リストの終端(先頭が0)でなければ繰り返す
		putnum(getvalue(clp), 0);//行番号を表示
		c_putch(' ');
		putlist(clp + 3);
		if(err){
			break;
		}
		newline();
		clp += *clp;
	}
}

void inew(void){
	unsigned char i;

	for(i = 0; i < 26; i++)//変数を初期化
		var[i] = 0;
	gstki = 0;//スタックを初期化
	lstki = 0;//スタックを初期化
	*listbuf = 0;
	clp = listbuf;//行ポインタを初期化
}

//clpが指し示す1行のステートメントを実行
//ステートメントに分岐があったら複数行を実行
//次に実行するべき行の先頭を返す
//エラーを生じたときはNULLを返す
unsigned char* iexe(){
	short lineno;
	unsigned char cd;
	unsigned char* lp;
	short vto, vstep;
	short index;//こんな大きさはいらないがエラーを防ぐため

	while(1){
		if(c_kbhit()){
			if(c_getch() == 27){
				while(*clp){
					clp += *clp;
				}
				err = ERR_ESC;
				return clp;
			}
		}

		switch(*cip){
		case I_GOTO:
			cip++;
			lineno = iexp();
			clp = getlp(lineno);
			if(lineno != getvalue(clp)){
				err = ERR_ULN;
				return NULL;
			}
			cip = clp + 3;
			continue;
		case I_GOSUB:
			cip++;
			lineno = iexp();//行番号を取得
			if(err) return NULL;
			lp = getlp(lineno);
			if(lineno != getvalue(lp)){
				err = ERR_ULN;
				return NULL;
			}
			gpush(clp);
			gpush(cip);
			if(err) return NULL;
			clp = lp;
			cip = clp + 3;
			continue;
		case I_RETURN:
			cip = gpop();
			lp = gpop();//スタックのエラーで行ポインタを失わないように
			if(err) return NULL;
			clp = lp;
			break;
		case I_FOR:
			cip++;
			if(*cip++ != I_VAR){//次が変数でなければ打ち切り
				err = ERR_FORWOV;
				return NULL;
			}
			index = *cip;//インデクスを取得
			ivar();//変数に値を代入してポインタを進める
			if(err) return NULL;

			if(*cip == I_TO){
				cip++;
				vto = iexp();//終了値を取得してポインタを進める
			} else {
				err = ERR_FORWOTO;
				return NULL;
			}
			if(*cip == I_STEP){//もしSTEPがあれば
				cip++;
				vstep = iexp();//ステップ数を取得してポインタを進める
			} else {//STEPがなければ
				vstep = 1;//ステップ数を既定値に設定
			}

			lpush(clp);//行ポインタを退避
			lpush(cip);//中間コードポインタを退避
			lpush((unsigned char*)vto);//終了値をポインタとごまかして退避
			lpush((unsigned char*)vstep);//増分をポインタとごまかして退避
			lpush((unsigned char*)index);//変数名(変数番号)をポインタとごまかして退避
			if(err) return NULL;
			break;
		case I_NEXT:
			cip++;
			if(*cip++ !=I_VAR){//次が変数でなければ打ち切り
				err = ERR_NEXTWOV;
				return NULL;
			}

			if(lstki < 5){
				err = ERR_LSTKUF;
				return NULL;
			}
			//まだスタックの状態を変更しない
			index = (short)lstk[lstki - 1];//インデクスを取得
			if(index != *cip){//インデクスが対応しなければ
				err = ERR_NEXTUM;
				return NULL;
			}
			cip++;
			vstep = (short)lstk[lstki - 2];
			var[index] += vstep;

			vto = (short)lstk[lstki - 3];
			if(	((vstep < 0) && (var[index] < vto)) ||
				((vstep > 0) && (var[index] > vto))){//この条件が成立したら繰り返しを終了
				lstki -= 5;//スタックを捨てる
				break;
			}
			cip = lstk[lstki - 4];
			clp = lstk[lstki - 5];
			continue;//break;

		case I_IF:
			cip++;
			cd = iif();
			if(err){
				err = ERR_IFWOC;//エラー番号の書き換え
				return NULL;
			}
			if(cd)
				continue;
			//条件不成立の処理はREMへ流れる
		case I_REM:
			//ポインタを行末(I_EOL、38)まで進める
			//38が本当に行末かどうかはわからないが(数字の38かもしれない)、結果は同じ
			while(*cip != I_EOL) cip++;
			break;
		case I_STOP:
			while(*clp){
				clp += *clp;
			}
			return clp;
		case I_INPUT:
			cip++;
			iinput();
			break;
		case I_PRINT:
			cip++;
			iprint();
			break;
		case I_LET:
			cip++;
			ilet();
			break;
        case I_LED:
			cip++;
			iled();
            break;
		case I_VAR:
			cip++;
			ivar();
			break;
		case I_ARRAY:
			cip++;
			iarray();
			break;
		case I_LIST:
		case I_NEW:
		case I_RUN:
		case I_SAVE:
		case I_LOAD:
			err = ERR_COM;
			return NULL;
		}

		switch(*cip){
		case I_SEMI://セミコロンなら継続
			cip++;
			break;
		case I_EOL://行末なら繰り返しを終了
			return clp + *clp;
		default://どれでもなければ行末エラー
			//c_puts("iexe:");putnum(*cip,0);newline();//DEBUG
			err = ERR_SYNTAX;
			return NULL;
		}
	}
}

void irun(){
	unsigned char* lp;//次に実行する行を一時的に保持

	gstki = 0;//スタックを初期化
	lstki = 0;//スタックを初期化
	clp = listbuf;
	
	while(*clp){//末尾に到達するまで繰り返す
		cip = clp + 3;//ステートメントの先頭
		lp = iexe();//カレント行を実行して次の行を得る
		if(err){//実験中
		//if(lp == NULL){
			return;
		}
		clp = lp;
	}
}

void icom(){
	cip = ibuf;
	switch(*cip){//これらのコマンドはマルチステートメントで記述できません
	case I_LIST:
		cip++;
		if(*cip == I_EOL || *(cip + 3) == I_EOL)
			ilist();
		else
			err = ERR_COM;
		break;
	case I_NEW:
		cip++;
		if(*cip == I_EOL)
			inew();
		else
			err = ERR_COM;
		break;
	case I_RUN:
		cip++;
		irun();//runはうしろにゴミがあってもいい
		break;
	case I_SAVE://extend
		cip++;
		if(*cip == I_BOOT){
			cip++;
			listbuf[SIZE_LIST] = I_BOOT;
		} else {
			listbuf[SIZE_LIST] = 0;
		}
		if(*cip == I_EOL)
			flash_write(listbuf);
		else
			err = ERR_COM;
		break;
	case I_LOAD://extend
        cip++;
		if(*cip == I_EOL)
			flash_read(listbuf);
		else
			err = ERR_COM;
		break;
	default:
		iexe();//上記のどれも実行されなかった場合
		break;
	}

	if(err && err != ERR_ESC){
		if(cip >= listbuf && cip < listbuf + SIZE_LIST && *clp)
		{
			newline(); c_puts("ERR LINE:");
			putnum(getvalue(clp), 0);//行番号を表示
			c_putch(' ');
			putlist(clp + 3);
		}
		else 
		{
			newline(); c_puts("YOU TYPE: ");
			putlist(ibuf);
		}
	}
	
}

void error(){
	newline();
	c_puts(errmsg[err]);
	newline();
	err = 0;
}

void basic(void){
	unsigned char len;

    // 乱数のタネのテスト
    //#include <xc.h>
    //c_puts("RCOUNT:");putnum(RCOUNT, 0);newline();
    //c_puts("DISICNT:");putnum(DISICNT, 0);newline();
    //c_puts("SPLIM:");putnum(SPLIM, 0);newline();
    //c_puts("ADC1BUF0:");putnum(ADC1BUF0, 0);newline();
    //c_puts("ADC1BUFF:");putnum(ADC1BUFF, 0);newline();
    //c_puts("U2TXREG:");putnum(U2TXREG, 0);newline();
    //c_puts("RTCVAL:");putnum(RTCVAL, 0);newline();
    //c_puts("ALRMVAL:");putnum(ALRMVAL, 0);newline();
    //c_puts("TMR2:");putnum(TMR2, 0);newline();

	inew();//状態を初期化
	c_puts("TOYOSHIKI TINY BASIC"); newline();// 起動メッセージを表示
	c_puts(STR_EDITION);
	c_puts(" EDITION"); newline();
	if(bootflag() == I_BOOT){
		c_puts("Power on run"); newline();
		flash_read(listbuf);
		irun();
	}
	error();//エラーがなければOKを表示、ここでエラーフラグをクリア

	//1行入力と処理の繰り返し
	while(1){
		c_putch('>'); // プロンプト
		c_gets(); // 1行入力
		len = toktoi();//中間コードバッファへ保存
		if(err){//もし変換できなければ
			newline(); c_puts("YOU TYPE:");
			c_puts(lbuf);
			error();
			continue;//何もしない
		}

		if(*ibuf == I_NUM){//先頭に数値があれば行番号とみなし
			*ibuf = len;//数値の中間コードを行の長さに置き換え
			inslist();//リストに保存
			if(err){
				error();//List buffer overflow
			}
		} else {
			icom();//先頭に数値がなければコマンドとして実行
			error();//エラーがなければOKを表示
		}
	}
}
