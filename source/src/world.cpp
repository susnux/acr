// world.cpp: core map management stuff

#include "pch.h"
#include "cube.h"
#include "bot/bot.h"


sqr *world = NULL;
int sfactor, ssize, cubicsize, mipsize;

header hdr;

// main geometric mipmapping routine, recursively rebuild mipmaps within block b.
// tries to produce cube out of 4 lower level mips as well as possible,
// sets defer to 0 if mipped cube is a perfect mip, i.e. can be rendered at this
// mip level indistinguishable from its constituent cubes (saves considerable
// rendering time if this is possible).

void remip(const block &b, int level)
{
	if(level>=SMALLEST_FACTOR) return;
	int lighterr = getvar("lighterror")*3;
	sqr *w = wmip[level];
	sqr *v = wmip[level+1];
	int wfactor = sfactor - level;
	int vfactor = sfactor - (level+1);
	block s = b;
	if(s.x&1) { s.x--; s.xs++; }
	if(s.y&1) { s.y--; s.ys++; }
	s.xs = (s.xs+1)&~1;
	s.ys = (s.ys+1)&~1;
	for(int y = s.y; y<s.y+s.ys; y+=2) for(int x = s.x; x<s.x+s.xs; x+=2)
	{
		sqr *o[4];
		o[0] = SWS(w,x,y,wfactor);							   // the 4 constituent cubes
		o[1] = SWS(w,x+1,y,wfactor);
		o[2] = SWS(w,x+1,y+1,wfactor);
		o[3] = SWS(w,x,y+1,wfactor);
		sqr *r = SWS(v,x/2,y/2,vfactor);						 // the target cube in the higher mip level
		*r = *o[0];
		uchar nums[MAXTYPE];
		loopi(MAXTYPE) nums[i] = 0;
		loopj(4) nums[o[j]->type]++;
		r->type = SEMISOLID;								// cube contains both solid and space, treated specially in the renderer
		loopk(MAXTYPE) if(nums[k]==4) r->type = k;
		if(!SOLID(r))
		{
			int floor = 127, ceil = -128, num = 0;
			loopi(4) if(!SOLID(o[i]))
			{
				num++;
				int fh = o[i]->floor;
				int ch = o[i]->ceil;
				if(r->type==SEMISOLID)
				{
					if(o[i]->type==FHF) fh -= o[i]->vdelta/4+2;	 // crap hack, needed for rendering large mips next to hfs
					if(o[i]->type==CHF) ch += o[i]->vdelta/4+2;	 // FIXME: needs to somehow take into account middle vertices on higher mips
				}
				if(fh<floor) floor = fh;  // take lowest floor and highest ceil, so we never have to see missing lower/upper from the side
				if(ch>ceil) ceil = ch;
			}
			r->floor = floor;
			r->ceil = ceil;
		}
		if(r->type==CORNER) goto mip;					   // special case: don't ever split even if textures etc are different
		r->defer = 1;
		if(SOLID(r))
		{
			loopi(3)
			{
				if(o[i]->wtex!=o[3]->wtex) goto c;		  // on an all solid cube, only thing that needs to be equal for a perfect mip is the wall texture
			}
		}
		else
		{
			loopi(3)
			{
				if(o[i]->type!=o[3]->type
				|| o[i]->floor!=o[3]->floor
				|| o[i]->ceil!=o[3]->ceil
				|| o[i]->ftex!=o[3]->ftex
				|| o[i]->ctex!=o[3]->ctex
				|| abs(o[i+1]->r-o[0]->r)>lighterr		  // perfect mip even if light is not exactly equal
				|| abs(o[i+1]->g-o[0]->g)>lighterr
				|| abs(o[i+1]->b-o[0]->b)>lighterr
				|| o[i]->utex!=o[3]->utex
				|| o[i]->wtex!=o[3]->wtex) goto c;
			}
			if(r->type==CHF || r->type==FHF)				// can make a perfect mip out of a hf if slopes lie on one line
			{
				if(o[0]->vdelta-o[1]->vdelta != o[1]->vdelta-SWS(w,x+2,y,wfactor)->vdelta
				|| o[0]->vdelta-o[2]->vdelta != o[2]->vdelta-SWS(w,x+2,y+2,wfactor)->vdelta
				|| o[0]->vdelta-o[3]->vdelta != o[3]->vdelta-SWS(w,x,y+2,wfactor)->vdelta
				|| o[3]->vdelta-o[2]->vdelta != o[2]->vdelta-SWS(w,x+2,y+1,wfactor)->vdelta
				|| o[1]->vdelta-o[2]->vdelta != o[2]->vdelta-SWS(w,x+1,y+2,wfactor)->vdelta) goto c;
			}
		}
		{ loopi(4) if(o[i]->defer) goto c; }			   // if any of the constituents is not perfect, then this one isn't either
		mip:
		r->defer = 0;
		c:;
	}
	s.x  /= 2;
	s.y  /= 2;
	s.xs /= 2;
	s.ys /= 2;
	remip(s, level+1);
}

void remipmore(const block &b, int level)
{
	block bb = b;
	if(bb.x>1) bb.x--;
	if(bb.y>1) bb.y--;
	if(bb.xs<ssize-3) bb.xs++;
	if(bb.ys<ssize-3) bb.ys++;
	remip(bb, level);
}

static int clentsel = 0, clenttype = NOTUSED;

void nextclosestent(void) { clentsel++; }

void closestenttype(char *what)
{
	clenttype = what[0] ? findtype(what) : NOTUSED;
}

COMMAND(nextclosestent, ARG_NONE);
COMMAND(closestenttype, ARG_1STR);

int closestent()		// used for delent and edit mode ent display
{
	if(noteditmode("closestent")) return -1;
	int best = -1, bcnt = 0;
	float bdist = 99999;
	loopj(3)
	{
		bcnt = 0;
		loopv(ents)
		{
			entity &e = ents[i];
			if(e.type==NOTUSED) continue;
			if(clenttype != NOTUSED && e.type != clenttype) continue;
			vec v(e.x, e.y, e.z);
			float dist = v.dist(camera1->o);
			if(j)
			{
				if(ents[best].x == e.x && ents[best].y == e.y && ents[best].z == e.z)
				{
					if(j == 2 && bcnt == clentsel) return i;
					bcnt++;
				}
			}
			else if(dist<bdist)
			{
				best = i;
				bdist = dist;
			}
		}
		if(best < 0 || bcnt == 1) break;
		if(bcnt) clentsel %= bcnt;
	}
	return best;
}

void entproperty(int prop, int amount)
{
	int n = closestent();
	if(n<0) return;
	entity &e = ents[n];
	switch(prop)
	{
		case 0: e.attr1 += amount; break;
		case 1: e.attr2 += amount; break;
		case 2: e.attr3 += amount; break;
		case 3: e.attr4 += amount; break;
		case 11: e.x += amount; break;
		case 12: e.y += amount; break;
		case 13: e.z += amount; break;
	}
	switch(e.type)
	{
		case LIGHT: calclight(); break;
		case SOUND: preloadmapsound(e);
	}
}

hashtable<char *, enet_uint32> mapinfo, &resdata = mapinfo;

void getenttype()
{
	int e = closestent();
	if(e<0) return;
	int type = ents[e].type;
	if(type < 0 || type >= MAXENTTYPES) return;
	result(entnames[type]);
}

int getentattr(int attr)
{
	int e = closestent();
	if(e>=0) switch(attr)
	{
		case 0: return ents[e].attr1;
		case 1: return ents[e].attr2;
		case 2: return ents[e].attr3;
		case 3: return ents[e].attr4;
	}
	return 0;
}

COMMAND(getenttype, ARG_NONE);
COMMAND(getentattr, ARG_1EXP);

void delent()
{
	int e = closestent();
	if(e<0) { conoutf("no more entities"); return; }
	int t = ents[e].type;
	conoutf("%s entity deleted", entnames[t]);
	ents[e].type = NOTUSED;
	addmsg(N_EDITENT, "ri9", e, NOTUSED, 0, 0, 0, 0, 0, 0, 0);
	switch(t)
	{
		case LIGHT: calclight(); break;
	}
}

int findtype(char *what)
{
	loopi(MAXENTTYPES) if(strcmp(what, entnames[i])==0) return i;
	conoutf("unknown entity type \"%s\"", what);
	return NOTUSED;
}

entity *newentity(int index, int x, int y, int z, char *what, int v1, int v2, int v3, int v4)
{
	int type = findtype(what);
	if(type==NOTUSED) return NULL;

	entity e(x, y, z, type, v1, v2, v3, v4);

	switch(type)
	{
		case LIGHT:
			if(v1>64) e.attr1 = 64;
			if(!v1) e.attr1 = 16;
			if(!v2 && !v3 && !v4) e.attr2 = 255;
			break;

		case MAPMODEL:
			e.attr4 = e.attr3;
			e.attr3 = e.attr2;
			e.attr2 = (uchar)e.attr1;
		case PLAYERSTART:
		case CTF_FLAG:
			e.attr2 = v1;
			e.attr1 = (int)camera1->yaw;
			break;
	}
	addmsg(N_EDITENT, "ri9", index<0 ? ents.length() : index, type, e.x, e.y, e.z, e.attr1, e.attr2, e.attr3, e.attr4);
	e.spawned = true;
	int oldtype = type;
	if(index<0) ents.add(e);
	else
	{
		oldtype = ents[index].type;
		ents[index] = e;
	}
	if(oldtype!=type) switch(oldtype)
	{
		case LIGHT: calclight(); break;
	}
	switch(type)
	{
		case LIGHT: calclight(); break;
		case SOUND: preloadmapsound(e); break;
	}
	return index<0 ? &ents.last() : &ents[index];
}

void entset(char *what, char *a1, char *a2, char *a3, char *a4)
{
	int n = closestent();
	if(n>=0)
	{
		entity &e = ents[n];
		newentity(n, e.x, e.y, e.z, what, ATOI(a1), ATOI(a2), ATOI(a3), ATOI(a4));
	}
}

COMMAND(entset, ARG_5STR);

void clearents(char *name)
{
	int type = findtype(name);
	if(noteditmode("clearents") || multiplayer()) return;
	loopv(ents)
	{
		entity &e = ents[i];
		if(e.type==type) e.type = NOTUSED;
	}
	switch(type)
	{
		case LIGHT: calclight(); break;
	}
}

COMMAND(clearents, ARG_1STR);

void scalecomp(uchar &c, int intens)
{
	int n = c*intens/100;
	if(n>255) n = 255;
	c = n;
}

void scalelights(int f, int intens)
{
	loopv(ents)
	{
		entity &e = ents[i];
		if(e.type!=LIGHT) continue;
		e.attr1 = e.attr1*f/100;
		if(e.attr1<2) e.attr1 = 2;
		if(e.attr1>32) e.attr1 = 32;
		if(intens)
		{
			scalecomp(e.attr2, intens);
			scalecomp(e.attr3, intens);
			scalecomp(e.attr4, intens);
		}
	}
	calclight();
}

COMMAND(scalelights, ARG_2INT);

int findentity(int type, int index)
{
	for(int i = index; i<ents.length(); i++) if(ents[i].type==type) return i;
	loopj(index) if(ents[j].type==type) return j;
	return -1;
}

int findentity(int type, int index, uchar attr2)
{
	for(int i = index; i<ents.length(); i++) if(ents[i].type==type && ents[i].attr2==attr2) return i;
	loopj(index) if(ents[j].type==type && ents[j].attr2==attr2) return j;
	return -1;
}

void nextplayerstart(char *type)
{
	static int cycle = -1;

	if(noteditmode("nextplayerstart")) return;
	cycle = type[0] ? findentity(PLAYERSTART, cycle + 1, atoi(type)) : findentity(PLAYERSTART, cycle + 1);
	if(cycle >= 0)
	{
		entity &e = ents[cycle];
		player1->o.x = e.x;
		player1->o.y = e.y;
		player1->o.z = e.z;
		player1->yaw = e.attr1;
		player1->pitch = 0;
		player1->roll = 0;
		entinmap(player1);
	}
}

COMMAND(nextplayerstart, ARG_1STR);

sqr *wmip[LARGEST_FACTOR*2];

void setupworld(int factor)
{
	ssize = 1<<(sfactor = factor);
	cubicsize = ssize*ssize;
	mipsize = cubicsize*134/100;
	sqr *w = world = new sqr[mipsize];
	memset(world, 0, mipsize*sizeof(sqr));
	loopi(LARGEST_FACTOR*2) { wmip[i] = w; w += cubicsize>>(i*2); }
}

bool empty_world(int factor, bool force)	// main empty world creation routine, if passed factor -1 will enlarge old world by 1
{
	if(!force && noteditmode("empty world")) return false;
	needsmap = false;

	sqr *oldworld = world;
	bool copy = false;
	if(oldworld && factor<0) { factor = sfactor+1; copy = true; }
	if(factor<SMALLEST_FACTOR) factor = SMALLEST_FACTOR;
	if(factor>LARGEST_FACTOR) factor = LARGEST_FACTOR;
	setupworld(factor);

	loop(x,ssize) loop(y,ssize)
	{
		sqr *s = S(x,y);
		s->r = s->g = s->b = 150;
		s->ftex = DEFAULT_FLOOR;
		s->ctex = DEFAULT_CEIL;
		s->wtex = s->utex = DEFAULT_WALL;
		s->type = SOLID;
		s->floor = 0;
		s->ceil = 16;
		s->vdelta = 0;
		s->defer = 0;
	}

	strncpy(hdr.head, "ACMP", 4);
	hdr.version = MAPVERSION;
	hdr.headersize = sizeof(header);
	hdr.sfactor = sfactor;

	if(copy)
	{
		loop(x,ssize/2) loop(y,ssize/2)
		{
			*S(x+ssize/4, y+ssize/4) = *SWS(oldworld, x, y, sfactor-1);
		}
		loopv(ents)
		{
			ents[i].x += ssize/4;
			ents[i].y += ssize/4;
		}
	}
	else
	{
		copystring(hdr.maptitle, "Untitled Map by Unknown", 128);
		hdr.waterlevel = -100000;
		setwatercolour();
		loopi(sizeof(hdr.reserved)/sizeof(hdr.reserved[0])) hdr.reserved[i] = 0;
		loopk(3) loopi(256) hdr.texlists[k][i] = i;
		ents.shrink(0);
		// Victor's fix for new map -> bots crashing
		loopv(players) if(players[i] && players[i]->pBot && players[i]->pBot){
			players[i]->pBot->ResetCurrentTask();
			// don't just be sure, be double sure
			players[i]->enemy = NULL;
			players[i]->pBot->m_pTargetEnt = NULL;
			players[i]->pBot->m_pTargetFlag = NULL;
		}
		block b = { 8, 8, ssize-16, ssize-16 };
		edittypexy(SPACE, b);
	}

	calclight();
	if(factor>=0) resetmap();
	if(oldworld)
	{
		delete[] oldworld;
		if(factor>=0)
		{
			//toggleedit();
			pushscontext(IEXC_MAPCFG);
			persistidents = false;
			execfile("config/default_map_settings.cfg");
			persistidents = true;
			popscontext();
			setvar("fullbright", 1);
			fullbrightlight();
		}
	}
	if(factor>=0)
	{
		// mapcenter
		player1->o.x = player1->o.y = (float)ssize/2;
		player1->o.z = 4;
		entinmap(player1);
		extern string clientmap;
		startmap(clientmap, false);
	}
	return true;
}

void mapenlarge()  { if(empty_world(-1, false)) addmsg(N_NEWMAP, "ri", -1); }
void newmap(int i) { if(empty_world(i, false)) addmsg(N_NEWMAP, "ri", max(i, 0)); }

COMMAND(mapenlarge, ARG_NONE);
COMMAND(newmap, ARG_1INT);
COMMANDN(recalc, calclight, ARG_NONE);
COMMAND(delent, ARG_NONE);
COMMAND(entproperty, ARG_2INT);

