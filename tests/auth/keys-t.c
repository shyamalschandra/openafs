/*
 * Copyright (c) 2010 Your File System Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR `AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*!
 * Tests for the afsconf key handling functions
 */

#include <afsconfig.h>
#include <afs/param.h>

#include <roken.h>

#include <afs/cellconfig.h>
#include <afs/keys.h>
#include <afs/afsutil.h>
#include <rx/rxkad.h>

static int
copy(char *inFile, char *outFile)
{
    int in, out;
    int code, written;
    char *block;
    size_t len;

    in = open(inFile, O_RDONLY);
    if (in<0)
	return EIO;

    out = open(outFile, O_WRONLY | O_CREAT, 0600);
    if (out<0)
	return EIO;

    block = malloc(1024);
    do {
	len = read(in, block, 1024);
	if (len > 0)
	    write(out, block, len);
    } while (len > 0);
    free(block);

    close(in);
    close(out);

    if (len == -1)
	return EIO;

    return 0;
}

int main(int argc, char **argv)
{
    struct afsconf_dir *dir;
    struct afsconf_keys keys;
    struct ktc_encryptionKey key;
    char buffer[1024];
    char *dirEnd;
    FILE *file;
    afs_int32 kvno;
    int code;
    int i;

    plan(61);

    /* Create a temporary afs configuration directory */
    snprintf(buffer, sizeof(buffer), "%s/afs_XXXXXX", gettmpdir());
    mkdtemp(buffer);
    dirEnd = buffer + strlen(buffer);

    /* Create a CellServDB file */
    strcpy(dirEnd, "/CellServDB");
    file = fopen(buffer, "w");
    fprintf(file, ">example.org # An example cell\n");
    fprintf(file, "127.0.0.1 #test.example.org\n");
    fclose(file);

    /* Create a ThisCell file */
    strcpy(dirEnd, "/ThisCell");
    file = fopen(buffer, "w");
    fprintf(file, "example.org\n");
    fclose(file);

    /* Firstly, copy in a known keyfile. */
    strcpy(dirEnd, "/KeyFile");
    code = copy("KeyFile", buffer);
    if (code)
	goto out;

    *dirEnd='\0';

    /* Start with a blank configuration directory */
    dir = afsconf_Open(strdup(buffer));
    ok(dir != NULL, "Sucessfully re-opened config directory");
    if (dir == NULL)
	goto out;

    /* Verify that GetKeys returns the entire set of keys correctly */
    code = afsconf_GetKeys(dir, &keys);
    is_int(0, code, "afsconf_GetKeys returns successfully");
    is_int(3, keys.nkeys, "... and returns the right number of keys");
    is_int(1, keys.key[0].kvno, " ... first key number is correct");
    is_int(2, keys.key[1].kvno, " ... second key number is correct");
    is_int(4, keys.key[2].kvno, " ... third key number is correct");
    ok(memcmp(keys.key[0].key, "\x01\x02\x04\x08\x10\x20\x40\x80", 8) == 0,
       " ... first key matches");
    ok(memcmp(keys.key[1].key, "\x04\x04\x04\x04\x04\x04\x04\x04", 8) == 0,
       " ... second key matches");
    ok(memcmp(keys.key[2].key, "\x19\x16\xfe\xe6\xba\x77\x2f\xfd", 8) == 0,
       " ... third key matches");

    /* Verify that GetLatestKey returns the newest key */
    code = afsconf_GetLatestKey(dir, &kvno, &key);
    is_int(0, code, "afsconf_GetLatestKey returns sucessfully");
    is_int(4, kvno, " ... with correct key number");
    ok(memcmp(&key, "\x19\x16\xfe\xe6\xba\x77\x2f\xfd", 8) == 0,
       " ... and correct key");

    /* Verify that random access using GetKey works properly */
    code = afsconf_GetKey(dir, 2, &key);
    is_int(0, code, "afsconf_GetKey returns successfully");
    ok(memcmp(&key, "\x04\x04\x04\x04\x04\x04\x04\x04", 8) == 0,
       " ... and with correct key");

    /* And that it fails if the key number doesn't exist */
    code = afsconf_GetKey(dir, 3, &key);
    is_int(code, AFSCONF_NOTFOUND,
	   "afsconf_GetKey returns not found for missing key");

    /* Check that AddKey can be used to add a new 'newest' key */
    code = afsconf_AddKey(dir, 5, "\x08\x08\x08\x08\x08\x08\x08\x08", 0);
    is_int(0, code, "afsconf_AddKey sucessfully adds a new key");

    /* And that we can get it back with GetKeys, GetLatestKey and GetKey */
    code = afsconf_GetKeys(dir, &keys);
    is_int(0, code, " ... and GetKeys still works");
    is_int(4, keys.nkeys, "... and has the correct number of keys");
    is_int(5, keys.key[3].kvno, " ... and the fourth key has the correct kvno");
    ok(memcmp(keys.key[3].key, "\x08\x08\x08\x08\x08\x08\x08\x08", 8) == 0,
       " ... and is the correct key");

    code = afsconf_GetLatestKey(dir, &kvno, &key);
    is_int(0, code, " ... and GetLatestKey returns successfully");
    is_int(5, kvno, " ... with the correct key number");
    ok(memcmp(&key, "\x08\x08\x08\x08\x08\x08\x08\x08", 8) == 0,
       " ... and the correct key");

    code = afsconf_GetKey(dir, 5, &key);
    is_int(0, code, " ... and GetKey still works");
    ok(memcmp(&key, "\x08\x08\x08\x08\x08\x08\x08\x08", 8) == 0,
       " ... and returns the correct key");

    /* Check that AddKey without the overwrite flag won't overwrite an existing
     * key */
    code = afsconf_AddKey(dir, 5, "\x10\x10\x10\x10\x10\x10\x10", 0);
    is_int(AFSCONF_KEYINUSE, code, "AddKey won't overwrite without being told to");

    /* Check with GetKey that it didn't */
    code = afsconf_GetKey(dir, 5, &key);
    is_int(0, code, " ... and GetKey still works");
    ok(memcmp(&key, "\x08\x08\x08\x08\x08\x08\x08\x08", 8) == 0,
       " ... and key hasn't been overwritten");

    /* Check that AddKey with the overwrite flag will overwrite an existing key */
    code = afsconf_AddKey(dir, 5, "\x10\x10\x10\x10\x10\x10\x10\x10", 1);
    is_int(0, code, "AddKey overwrites when asked");

    /* Use GetKey to check that it did so */
    code = afsconf_GetKey(dir, 5, &key);
    is_int(0, code, " ... and GetKey still works");
    ok(memcmp(&key, "\x10\x10\x10\x10\x10\x10\x10\x10", 8) == 0,
       " ... and key has been overwritten");

    /* Check that deleting a key that doesn't exist fails */
    code = afsconf_DeleteKey(dir, 6);
    is_int(AFSCONF_NOTFOUND, code,
           "afsconf_DeleteKey returns NOTFOUND if key doesn't exist");

    /* Check that we can delete a key using afsconf_DeleteKey */
    code = afsconf_DeleteKey(dir, 2);
    is_int(0, code, "afsconf_DeleteKey can delete a key");
    code = afsconf_GetKey(dir, 2, &key);
    is_int(AFSCONF_NOTFOUND, code, " ... and afsconf_GetKey can't find it");

    /* Check that deleting it doesn't leave a hole in what GetKeys returns */
    code = afsconf_GetKeys(dir, &keys);
    is_int(0, code, "... and afsconf_GetKeys returns it");
    is_int(3, keys.nkeys, "... and returns the right number of keys");
    is_int(1, keys.key[0].kvno, " ... first key number is correct");
    is_int(4, keys.key[1].kvno, " ... second key number is correct");
    is_int(5, keys.key[2].kvno, " ... third key number is correct");

    /* Make sure that if we drop the dir structure, and then rebuild it, we
     * still have the same KeyFile */
    afsconf_Close(dir);

    *dirEnd='\0';
    dir = afsconf_Open(strdup(buffer));
    ok(dir != NULL, "Sucessfully re-opened config directory");
    if (dir == NULL)
	goto out;

    code = afsconf_GetKeys(dir, &keys);
    is_int(0, code, "afsconf_GetKeys still works");
    is_int(3, keys.nkeys, "... and returns the right number of keys");
    is_int(1, keys.key[0].kvno, " ... first key number is correct");
    is_int(4, keys.key[1].kvno, " ... second key number is correct");
    is_int(5, keys.key[2].kvno, " ... third key number is correct");

    /* Now check that we're limited to 8 keys */
    for (i=0; i<5; i++) {
	code = afsconf_AddKey(dir, 10+i, "\x10\x10\x10\x10\x10\x10\x10\x10",
			      0);
	is_int(0, code, "Adding %dth key with AddKey works", i+4);
    }
    code = afsconf_AddKey(dir, 20, "\x10\x10\x10\x10\x10\x10\x10\x10",0);
    is_int(AFSCONF_FULL, code, "afsconf_AddKey fails once we've got 8 keys");

    /* Unlink the KeyFile */
    strcpy(dirEnd, "/KeyFile");
    unlink(buffer);

    /* Force a rebuild of the directory structure, just in case */
    afsconf_Close(dir);

    *dirEnd='\0';
    dir = afsconf_Open(strdup(buffer));
    ok(dir != NULL, "Sucessfully re-opened config directory");
    if (dir == NULL)
	goto out;

    /* Check that all of the various functions work properly if the file
     * isn't there */
    code = afsconf_GetKeys(dir, &keys);
    is_int(0, code, "afsconf_GetKeys works with an empty KeyFile");
    is_int(0, keys.nkeys, " ... and returns the right number of keys");
    code = afsconf_GetKey(dir, 1, &key);
    is_int(AFSCONF_NOTFOUND, code,
	   "afsconf_GetKey returns NOTFOUND with an empty KeyFile");
    code = afsconf_DeleteKey(dir, 1);
    is_int(AFSCONF_NOTFOUND, code,
	   "afsconf_DeleteKey returns NOTFOUND with an empty KeyFile");
    code = afsconf_GetLatestKey(dir, &kvno, &key);
    is_int(AFSCONF_NOTFOUND, code,
	   "afsconf_GetLatestKey returns NOTFOUND with an empty KeyFile");

    /* Now try adding a key to an empty file */
    code = afsconf_AddKey(dir, 1, "\x10\x10\x10\x10\x10\x10\x10\x10", 1);
    is_int(0, code, "afsconf_AddKey succeeds with an empty KeyFile");
    code = afsconf_GetLatestKey(dir, &kvno, &key);
    is_int(0, code, " ... and afsconf_GetLatestKey succeeds");
    is_int(1, kvno, " ... with correct kvno");
    ok(memcmp(&key, "\x10\x10\x10\x10\x10\x10\x10\x10", 8) == 0,
       " ... and key");

out:
    strcpy(dirEnd, "/KeyFile");
    unlink(buffer);
    strcpy(dirEnd, "/CellServDB");
    unlink(buffer);
    strcpy(dirEnd, "/ThisCell");
    unlink(buffer);
    strcpy(dirEnd, "/UserList");
    unlink(buffer);
    *dirEnd='\0';
    rmdir(buffer);
}
