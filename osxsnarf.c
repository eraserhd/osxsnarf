#undef nil

#include <u.h>
#include <Carbon/Carbon.h>
AUTOFRAMEWORK(Carbon)
#include <libc.h>
#include <thread.h>
#include <fcall.h>
#include <9p.h>

static void fsread(Req *);
static void fswrite(Req *);

enum {
	Qroot = 0,
	Qsnarf,
	SnarfSize = 64*1024,
	StackSize = 8 * 1024,
};

Srv fs=
{
	.read		=	fsread,
	.write		= 	fswrite,
};

static PasteboardRef appleclip;

static char snarf[3*SnarfSize+1];
static Rune rsnarf[SnarfSize+1];

static char *deflisten = "tcp!*!18001";

RWLock l;

void
usage(void)
{
	fprint(2, "usage: %s [-D] [listen]\n", argv0);
	sysfatal("usage");
}

void
fswrite(Req *r) 
{
	CFDataRef cfdata;
	PasteboardSyncFlags flags;
	char err[128];

	if((int)r->fid->file->aux != Qsnarf) {
		respond(r, "no such file or directory");
		return;
	}

	if(r->ifcall.offset >= SnarfSize) {
		sprintf(err, "writing too much for this buffer. max size is: %d", SnarfSize);
		respond(r, err);
		return;
	}

	wlock(&l);
	/* silently truncate here. perhaps better to return an error? */
	if(r->ifcall.offset + r->ifcall.count > SnarfSize)
		r->ifcall.count = SnarfSize - r->ifcall.offset; 	

	memmove(snarf+r->ifcall.offset, r->ifcall.data, r->ifcall.count);
	snarf[r->ifcall.offset+r->ifcall.count] = '\0';

	runesnprint(rsnarf, nelem(rsnarf), "%s", snarf);
	if(PasteboardClear(appleclip) != noErr){
		respond(r, "apple pasteboard clear failed");
		goto werr;
	}
	flags = PasteboardSynchronize(appleclip);
	if((flags&kPasteboardModified) || !(flags&kPasteboardClientIsOwner)){
		respond(r, "apple pasteboard cannot assert ownership");
		goto werr;
	}
	cfdata = CFDataCreate(kCFAllocatorDefault, (uchar*)rsnarf, runestrlen(rsnarf)*2);

	if(cfdata == nil){
		respond(r, "apple pasteboard cfdatacreate failed");
		goto werr;
	}
	if(PasteboardPutItemFlavor(appleclip, (PasteboardItemID)1,
		CFSTR("public.utf16-plain-text"), cfdata, 0) != noErr){
		respond(r, "apple pasteboard putitem failed");
		CFRelease(cfdata);
		goto werr;
	}
	CFRelease(cfdata);
	r->ofcall.count = r->ifcall.count;
	respond(r, nil);

werr:
	wunlock(&l);
	return;
}

void
fsread(Req *r) 
{
	CFDataRef cfdata;
	OSStatus err = noErr;
	ItemCount nItems;
	uint32_t i;

	if((int)r->fid->file->aux != Qsnarf) {
		respond(r, "no such file or directory");
		return;
	}

	rlock(&l);

	PasteboardSynchronize(appleclip);
	if((err = PasteboardGetItemCount(appleclip, &nItems)) != noErr) {
		respond(r, "apple pasteboard GetItemCount failed");
		goto rerr;
	}

	for(i = 1; i <= nItems; ++i) {
		PasteboardItemID itemID;
		CFArrayRef flavorTypeArray;
		CFIndex flavorCount;

		if((err = PasteboardGetItemIdentifier(appleclip, i, &itemID)) != noErr){
			respond(r, "can't get pasteboard item identifier");
			goto rerr;
		}

		if((err = PasteboardCopyItemFlavors(appleclip, itemID, &flavorTypeArray))!=noErr){
			respond(r, "Can't copy pasteboard item flavors");
			goto rerr;
		}

		flavorCount = CFArrayGetCount(flavorTypeArray);
		CFIndex flavorIndex;
		for(flavorIndex = 0; flavorIndex < flavorCount; ++flavorIndex){
			CFStringRef flavorType;
			flavorType = (CFStringRef)CFArrayGetValueAtIndex(flavorTypeArray, flavorIndex);
			if (UTTypeConformsTo(flavorType, CFSTR("public.utf16-plain-text"))){
				if((err = PasteboardCopyItemFlavorData(appleclip, itemID,
					CFSTR("public.utf16-plain-text"), &cfdata)) != noErr){
					respond(r, "apple pasteboard CopyItem failed");
					goto rerr;
				}
				CFIndex length = CFDataGetLength(cfdata);
				if (length > sizeof rsnarf) length = sizeof rsnarf;
				CFDataGetBytes(cfdata, CFRangeMake(0, length), (uint8_t *)rsnarf);
				snprint(snarf, sizeof snarf, "%.*S", length/sizeof(Rune), rsnarf);
				char *s = snarf;
				while (*s) {
					if (*s == '\r') *s = '\n';
					s++;
				}
				CFRelease(cfdata);
			}
		}
	}

	readstr(r, snarf);
	respond(r, nil);	

rerr:
	runlock(&l);
}

void
threadmain(int argc, char **argv)
{
	File *rootf;
	char *lstn = deflisten;

	ARGBEGIN{
	case 'D':
		chatty9p++;
		break;
		break;
	default:
		usage();
	}ARGEND
	if(argc == 1)
		lstn = argv[0];
	else if(argc > 1)
		usage();

	fs.tree = alloctree(getuser(), getuser(), DMDIR|0555, nil);
	rootf = createfile(fs.tree->root, "snarf", getuser(), 0666, nil);
	if(rootf == nil)
		sysfatal("creating snarf: %r");
	rootf->aux = (void *)Qsnarf;

	if(PasteboardCreate(kPasteboardClipboard, &appleclip) != noErr)
		sysfatal("pasteboard create failed");

	threadpostmountsrv(&fs, lstn, nil, MREPL|MCREATE);

	threadexits(nil);
}
