/* -*- c-basic-offset: 4; indent-tabs-mode: nil -*-
 * $Id$
 *
 * XASTIR, Amateur Station Tracking and Information Reporting
 * Copyright (C) 1999,2000  Frank Giannandrea
 * Copyright (C) 2000-2003  The Xastir Group
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * Look at the README for more information on the program.
 *
 */
/*
 * This is a library of Awk-like functions to facilitate, for example,
 * canonicalizing DBF attributes for shapefiles into internal Xastir
 * values when rendering shapefile maps, or rewriting labels
 * (e.g. callsigns into tactical calls), etc.
 *
 * #define TEST to build a simple test program.
 *
 * Uses Philip Hazel's Perl-compatible regular expression library (pcre).  
 * See www.pcre.org.
 *
 * Alan Crosswell, n2ygk@weca.org
 *
 * TODO
 *   permit embedded ;#} inside string assignment (balance delims)
 *   implement \t, \n, \0[x]nn etc.
 */
#include "config.h"
#ifdef HAVE_LIBPCRE
#include <stdio.h>
#include "awk.h"

#define min(a,b) ((a)<(b)?(a):(b))

/*
 * Symbol table
 *
 * Symbols $0-$9 are set by the results of the pcre pattern match.
 * Other symbols are declared by the caller and bound to variables
 * in the caller's program.  Make sure they are still in scope when
 * the pattern matcher is invoked!
 *
 * This assumes a very small symbol table, so it is searched linearly.
 * No fancy hash table lookups are needed.
 */

#define MAXSUBS 10              /* $0 thru $9 should be plenty */

/*
 * awk_new_symtab: alloc a symbol table with $0-$9 pre-declared.
 */
awk_symtab *awk_new_symtab()
{
    awk_symtab *n = calloc(1,sizeof(awk_symtab));
    static char sym[MAXSUBS][2];
    int i;

    for (i = 0; i < MAXSUBS; i++) {
        sym[i][0] = i+'0';
        sym[i][1] = '\0';
        awk_declare_sym(n,sym[i],STRING,NULL,0); /* just reserve the name */
    }
    return n;
}

void awk_free_symtab(awk_symtab *s)
{
    awk_symbol *p;

    for (p = s->head; p; ) {
        awk_symbol *x = p;
        p = p->next_sym;
        awk_free_sym(x);
    }
}

/*
 * awk_new_sym: alloc a symbol (symtab entry) and add to this symtab.
 */
awk_symbol *awk_new_sym(awk_symtab *this)
{
    awk_symbol *n = calloc(1,sizeof(awk_symbol));

    if (this->last)
        this->last->next_sym = n;
    else {
        this->head = n;
    }
    this->last = n;
    return n;
}

void awk_free_sym(awk_symbol *s)
{
    if (s)
        free(s);
}

/*
 * awk_declare_sym: declare a symbol and bind to storage for its value.
 */
int awk_declare_sym(awk_symtab *this,
                const char *name, 
                enum awk_symtype type,
                const void *val,
                const int size)
{
    awk_symbol *s = awk_new_sym(this);

    if (!s)
        return -1;
    s->name = name;
    s->namelen = strlen(name);
    s->type = type;
    s->val = (void *)val;
    s->size = size;
    s->len = 0;
    return 0;
}

/*
 * awk_find_sym: search symtab for symbol
 */
awk_symbol *awk_find_sym(awk_symtab *this,
                 const char *name,
                 const int len)
{
    awk_symbol *s;

    for (s = this->head; s; s = s->next_sym)
        if ((s->namelen == len) && (strncasecmp(s->name,name,len) == 0))
            return s;
    return NULL;
}

/*
 * awk_set_sym: set a symbol's value (writes into bound storage).
 * Returns -1 if it was unable to (symbol not found).
 */
int awk_set_sym(awk_symbol *s,
            const char *val, 
            const int len)
{
    int l = len + 1;
    int minlen = min(s->size-1,l);

    if (!s)
        return -1;
    switch(s->type) {
    case STRING:
        if (minlen > 0)
            strncpy(s->val,val,minlen);
        s->len = l - 1;
        break;
    case INT:
        *((int *)s->val) = atoi(val);
        s->len = sizeof(int);
        break;
    case FLOAT:
        *((double *)s->val) = atof(val);
        s->len = sizeof(double);
        break;
    default:
        return -1;
        break;
    }
    return 0;
}

/* 
 * awk_get_sym: copy (and cast) symbol's value into supplied string buffer 
 */
int awk_get_sym(awk_symbol *s,          /* symbol */
            char *store,        /* store result here */
            int size,           /* sizeof(*store) */
            int *len)           /* store length here */
{
    int minlen;
    char cbuf[128];             /* conversion buffer for int/float */
    int cbl;

    if (!s)
        return -1;
    *store = '\0';
    *len = 0;
    switch(s->type) {
    case STRING:
        if (s->len > 0) {
            minlen = min(s->len,size-1);
            strncpy(store,s->val,minlen);
            *len = minlen;
        } else
            *len = 0;
        break;
    case INT:
        if (s->len > 0) {
            sprintf(cbuf,"%d",*((int *)s->val));
            cbl = strlen(cbuf);
            minlen = min(cbl,size-1);
            strncpy(store,cbuf,minlen);
            *len = minlen;
        } else
            *len = 0;
        break;
    case FLOAT:
        if (s->len > 0) {
            sprintf(cbuf,"%f",*((double *)s->val));
            cbl = strlen(cbuf);
            minlen = min(cbl,size-1);
            strncpy(store,cbuf,minlen);
            *len = minlen;
        } else
            *len = 0;
        break;
    }
    return 0;
}


/*
 *  Action compilation and interpretation.
 *
 *  Action grammar is:
 *  <attr_set> := <attr> "=" value 
 *  <op>   := "next"   # like awk, to end pattern search
 *  <stmt> := <attr_set> | <op>
 *  <stmt_list> := <stmt_list> ";" <stmt> | <stmt>
 *  <action> := <stmt_list> 
 *
 * It's a trivial grammar so no need for yacc/bison.
 */

/*
 * awk_compile_stmt: "Compiles" a single action statement.
 */
awk_compile_stmt(awk_symtab *this,
             awk_action *p,
             const char *stmt,
             int len)
{
    const char *s = stmt, *op, *ep;
  
    while (isspace(*s)) {               /* clean up leading white space */
        ++s;
        --len;
    }
    ep = &s[len];

    if (op = strchr(s,'=')) {   /* it's either an assignment */
        const char *val = op+1;
        while (isspace(*val)) {
            val++;
            len--;
        }
        --op;
        while (isspace(*op) && op>s) op--;
        p->opcode = ASSIGN;
        p->dest = awk_find_sym(this,s,(op-s+1));
        if (!p->dest) {
            return -1;
        }
        p->expr = val;
        p->exprlen = (ep-val);
    } else {                    /* or the "next" keyword */
        const char *r;

        for (r=&s[len-1]; isspace(*r); r--,len--) ; /* trim trailing white space */
        if (len == 4 && strncmp(s,"next",4) == 0) {
            p->opcode = NEXT;
        } else {                /* failed to parse */
            return -1;
        }
    }
    return 0;
}

/*
 * awk_compile_action: Break the action up into stmts and compile them
 *  and link them together.
 */
awk_action *awk_compile_action(awk_symtab *this, const char *act)
{
    awk_action *p, *first = calloc(1,sizeof(awk_action));
    const char *cs,*ns;         /* current, next stmt */

    p = first;
    if (!p)
        return NULL;

    for (cs = ns = act; ns && *ns; cs = (*ns==';')?ns+1:ns) {
        ns = strchr(cs,';');
        if (!ns)                        /* end of string */
            ns = &cs[strlen(cs)];
        if (awk_compile_stmt(this,p,cs,(ns-cs)) >= 0) {
            p->next_act = calloc(1,sizeof(awk_action));
            p = p->next_act;
        }
    } 
    return first;
}

/* 
 * awk_eval_expr: expand $vars into dest and do type conversions as
 *  needed.  For strings, just write directly into dest->val.  For
 *  ints/floats, write to a temp buffer and then atoi() or atof().
 */
void awk_eval_expr(awk_symtab *this,
               awk_symbol *dest, 
               const char *expr,
               int exprlen)
{
    int i,dmax,dl,newlen;
    char c,delim;
    char *dp;
    const char *symname;
    int done;
    char tbuf[128];
    awk_symbol *src;

    if (dest && expr) {
        if (dest->type == STRING) {
            dp = dest->val; /* just expand directly to result buffer */
            dmax = dest->size;
        } else {
            dp = tbuf;          /* use temp buffer */
            dmax = sizeof(tbuf);
        }
        for (done = 0, i = 0, dl = 0; !done && i < dmax && exprlen > 0; i++) {
            switch (c = *expr) {
            case '"':
            case '\'':          /* trim off string delims */
                ++expr;
                --exprlen;
                if (expr[exprlen-1] == c) /* look for matching close delim */
                    --exprlen;
                break;
            case '$':        /* $... look for variable substitution */
                if (--exprlen < 0) {
                    done = 1;
                    break;
                }
                c = *++expr;            /* what's after the $? */
                switch (c) {
                case '{':       /* ${var}... currently broken (see TODO) */
                    delim='}';
                    ++expr;     /* skip the open delim */
                    --exprlen;
                    break;
                case '(':       /* $(var)... */
                    delim=')';
                    ++expr;
                    --exprlen;
                    break;
                default:        /* $var ... */
                    delim='\0';
                    break;
                }
                /* now search for the var name using closing delim */
                symname = expr;
                if (delim == '\0') {    /* no close delim */
                    while (!isspace(*expr) && !ispunct(*expr) && exprlen > 0) {
                        ++expr;
                        --exprlen;
                    }
                } else {                /* search for close delim */
                    while (*expr != delim && exprlen > 0) {
                        ++expr;
                        --exprlen;
                    }
                }
                src = awk_find_sym(this,symname,(expr-symname));
                if (delim) {    /* gotta skip over the close delim */
                    ++expr;
                    --exprlen;
                }
                /* make sure src and dest of string copy don't overlap */
                if (src->type == STRING 
                    && dp >= (char *)src->val 
                    && dp <= &((char *)src->val)[src->size]) {
                    char *sp;

                    if (sizeof(tbuf) >= src->size) { /* tbuf big enuf */
                        sp = tbuf;
                    } else {    /* tbuf too small */
                        sp = malloc(src->size);
                        if (!sp) /* oh well! */
                            break; 
                    }
                    awk_get_sym(src,sp,src->size,&newlen);
                    bcopy(sp,dp,newlen); /* now copy it in */
                } else {
                    awk_get_sym(src,dp,(dmax-dl),&newlen);
                }
                dl += newlen;
                dp += newlen;
                break;
            case '\\':          /* \... quote next char */
                /* XXX TODO: implement \n,\t,\0[x].. etc. */
                if (--exprlen < 0) {
                    done = 1;
                } else {
                    if (dl < dmax) {    
                        *dp++ = *expr++;        /* copy the quoted char */
                        ++dl;
                    }
                }
                break;
            default:                    /* just copy the character */
                if (--exprlen < 0) {
                    done = 1;
                } else {
                    if (dl < dmax) {    
                        *dp++ = *expr++;        /* copy the char */
                        ++dl;
                    }
                }
                break;
            }   /* end switch (*expr) */
        } /* end for loop */
        *dp = '\0';                     /* null terminate the string */
        switch(dest->type) {
        case INT:
            if (dest->size >= sizeof(int)) {
                *((int *)dest->val) = atoi(tbuf);
                dest->len = sizeof(int);
            }
            break;
        case FLOAT:
            if (dest->size >= sizeof(double)) {
                *((double *)dest->val) = atof(tbuf);
                dest->len = sizeof(double);
            }
            break;
        case STRING:            /* already filled val in */
            dest->len = dl;     /* just update len */
            break;
        default:
            break;
        }
    }
}

/*
 * awk_exec_action: interpret the compiled action.
 */
awk_exec_action(awk_symtab *this, const awk_action *code)
{
    const awk_action *p;
    int done = 0;

    for (p = code; p && !done; p = p->next_act) {
        char *evaled;
        switch (p->opcode) {
        case NEXT:
            done = 1;
            break;
        case ASSIGN:
            awk_eval_expr(this,p->dest,p->expr,p->exprlen);
            break;
        case NOOP:
            break;
        default:
            break;
        }
    }
    return done;
}

/*
 * Rules consists of pcre patterns and actions.  A program is
 *  the collection of rules to apply as a group.
 */

/*
 * awk_new_rule: alloc a rule
 */
awk_rule *awk_new_rule()
{
    awk_rule *n = calloc(1,sizeof(awk_rule));
    return n;
}
void awk_free_rule(awk_rule *r)
{
    if (r) {
        if (r->flags&AR_MALLOC) {
            if (r->act)
                free((char *)r->act);
            if (r->pattern)
                free((char *)r->pattern);
        }
        free(r);
    }
}

/*
 * awk_new_program: alloc a program
 */
awk_program *awk_new_program()
{
    awk_program *n = calloc(1,sizeof(awk_program));
    return n;
}
void awk_free_program(awk_program *rs)
{
    awk_rule *r;

    if (rs) {
        for (r = rs->head; r; ) {
            awk_rule *x = r;
            r = r->next_rule;
            awk_free_rule(x);
        }
        free(rs);
    }
}

/*
 * awk_add_rule: add a rule to a program
 */
void awk_add_rule(awk_program *this, awk_rule *r)
{
    if (!this)
        return;
    if (!this->last) {
        this->head = this->last = r;
        r->next_rule = NULL;
    } else {
        this->last->next_rule = r;
        this->last = r;
    }
}

/*
 * awk_load_program_array:  load program from an array of rules.  Use this
 *  to load a program from a statically declared array (see test main
 *  program for an example).
 */
awk_program *awk_load_program_array(awk_symtab *this, /* symtab that goes w/this program */
                         awk_rule rules[], /* rules array */
                         int nrules) /* size of array */
{
    awk_program *n = awk_new_program();
    awk_rule *r,*pr; 

    if (!n)
        return NULL;

    n->symtbl = this;
    for (r = rules; r < &rules[nrules]; r++) {
        awk_add_rule(n,r);
    }
    return n;
}

/*
 * awk_load_program_file:  load program from a file.
 *
 * File syntax is a simplified version of awk:
 *
 * {action}
 * /pattern/ {action}
 * BEGIN {action}
 * END {action}
 * # comments...
 * (blank lines)
 *
 * Note that action can continue onto subsequent lines.
 */
static void garbage(const char *file, 
                    int line, 
                    const char *buf, 
                    const char *cp)
{
    fprintf(stderr,"%s:%d: parse error:\n",file,line);
    fputs(buf,stderr);
    fputc('\n',stderr);
    while (cp-- > buf)
        fputc(' ',stderr);
    fputs("^\n\n",stderr);
}

static char *dupe(char *s) 
{
    int l = strlen(s);
    char *r = malloc(l+1);
    
    if (r)
        strcpy(r,s);
    return r;
}

awk_program *awk_load_program_file(awk_symtab *this, /* symtab for this program */
                        const char *file) /* rules filename */
{
    awk_program *n = awk_new_program();
    awk_rule *r,*pr; 
    FILE *f = fopen(file,"r");
    char in[1024];
    int line = 0;

    if (!f) {
        perror(file);
        return NULL;
    }

    if (!n)
        return NULL;

    n->symtbl = this;
    while (fgets(in,sizeof(in),f)) {
        char *cp = in, *p;
        int l = strlen(in);

        ++line;
        if (in[l-1] == '\n')
            in[--l] = '\0';
        while (isspace(*cp)) ++cp;
        switch(*cp) {
        case '\0':              /* empty line */
            continue;
        case '#':               /* comment line */
            continue;
        case '/':               /* begin regexp */
            r = awk_new_rule();
            r->ruletype = REGEXP;
            p = ++cp;;              /* now points at pattern */
        more:
            while (*cp && *cp != '/') ++cp; /* find end of pattern */
            if (cp > in && cp[-1] == '\\') { /* '/' quoted */
                ++cp;
                goto more;      /* so keep going */
            }
            if (*cp != '\0')    /* zap end of pattern */
                *cp++ = '\0';
            r->pattern = dupe(p);
            break;
        case 'B':               /* BEGIN? */
            if (strncmp(cp,"BEGIN",5) == 0) {
                r = awk_new_rule();
                r->ruletype = BEGIN;
                cp += 5;        /* strlen("BEGIN") */
            } else {
                garbage(file,line,in,cp);
                continue;
            }
            break;
        case 'E':               /* END? */
            if (strncmp(cp,"END",3) == 0) {
                r = awk_new_rule();
                r->ruletype = END;
                cp += 3;        /* strlen("END") */
            } else {
                garbage(file,line,in,cp);
                continue;
            }
            break;
        default:
            garbage(file,line,in,cp);
            continue;
        }
        while (isspace(*cp)) ++cp; /* skip whitespace */
        if (*cp == '{') {
            p = ++cp;
        loop: 
            while (*cp && *cp != '}' && *cp != '#') ++cp;
            if (*cp == '\0' || *cp == '#') { /* continues on next line */
                *cp++=' ';       /* replace \n w/white space */
                if (cp >= &in[sizeof(in)-1]) {
                    garbage(file,line,"line too long",0);
                    return n;
                }
                if (!fgets(cp,sizeof(in)-(cp-in),f)) {
                    fprintf(stderr,"%s:%d: failed to parse\n",file,line);
                    return n;
                }
                ++line;
                goto loop;      /* keep looking for that close bracket */
            }
            if (*cp != '\0')    /* zap end of act */
                *cp++ = '\0';
            r->act = dupe(p);
            r->flags |= AR_MALLOC;
            /* make sure there's no extraneous junk on the line */
            while (*cp && isspace(*cp)) ++cp;
            if (*cp == '#' || *cp == '\0')
                awk_add_rule(n,r);
            else {
                garbage(file,line,in,cp);
                continue;
            }
        } else {
            garbage(file,line,in,cp);
            continue;
        }
    } /* end while */
    fclose(f);
    return n;
}

/*
 * awk_compile_program: Once loaded (from array or file), the program is compiled.
 */
awk_compile_program(awk_program *rs)
{
    pcre *re;
    pcre_extra *pe;
    const unsigned char *tables;
    const char *error;
    awk_rule *r;
    int erroffset;

    if (!rs)
        return -1;
    tables = pcre_maketables(); /* NLS locale parse tables */

    for (r = rs->head; r; r = r->next_rule) {
        if (r->ruletype == REGEXP) {
            r->re = pcre_compile(r->pattern, /* the pattern */
                                 0,         /* default options */
                                 &error,    /* for error message */
                                 &erroffset,        /* for error offset */
                                 tables);   /* NLS locale character tables */
            if (!re) {
                int i;
                
                fprintf(stderr,"parse error: %s\n",r->pattern);
                fprintf(stderr,"             ");
                for (i = 0; i < erroffset; i++)
                    fputc(' ',stderr);
                fprintf(stderr,"^\n");
                return -1;
            }
            pe = pcre_study(re, 0, &error); /* optimize the regexp */
        } else if (r->ruletype == BEGIN) {
            rs->begin = r;
        }else if (r->ruletype == END) {
            rs->end = r;
        }
        r->code = awk_compile_action(rs->symtbl,r->act); /* compile the action */
    }
    return 0;
}

/*
 * awk_exec_program: apply the program to the given buffer
 */
int awk_exec_program(awk_program *this, char *buf, int len)
{
    int i,rc,done = 0;
    awk_rule *r;
    int ovector[3*MAXSUBS];
#define OVECLEN (sizeof(ovector)/sizeof(ovector[0]))

    if (!this || !buf || len <= 0)
        return 0;
    
    for (r = this->head; r && !done ; r = r->next_rule) {
        if (r->ruletype == REGEXP) {
            rc = pcre_exec(r->re,r->pe,buf,len,0,0,ovector,OVECLEN);
            /* assign values to as many of $0 thru $9 as were set */
            for (i = 0; rc > 0 && i < rc && i < MAXSUBS ; i++) {
                int ret;
                char symname[10];
                awk_symbol *s;
                
                sprintf(symname,"%d",i);
                s = awk_find_sym(this->symtbl,symname,1);
                s->val = &buf[ovector[2*i]];
                s->len = ovector[2*i+1]-ovector[2*i];
            }
            /* clobber the remaining $n thru $9 */
            for (; i < MAXSUBS; i++) {
                char symname[10];
                awk_symbol *s;
                
                sprintf(symname,"%d",i);
                s = awk_find_sym(this->symtbl,symname,1);
                s->len = 0;
            }
            if (rc > 0) {
                done = awk_exec_action(this->symtbl,r->code);
            }
        }
    }
    return done;
}


/*
 * awk_exec_begin: run the special BEGIN rule, if any
 */
int awk_exec_begin(awk_program *this)
{
    if (this && this->begin)
        return awk_exec_action(this->symtbl,this->begin->code);
    else
        return 0;
}


/*
 * awk_exec_end: run the special END rule, if any
 */
int awk_exec_end(awk_program *this)
{
    if (this && this->end)
        return awk_exec_action(this->symtbl,this->end->code);
    else
        return 0;
}

#endif /* HAVE_LIBPCRE */

