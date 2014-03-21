/////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2014, Daniel Reiter Horn
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are permitted
// provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice, this list of
//    conditions and the following disclaimer in the documentation and/or other materials
//    provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
// AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//////////////////////////////////////////////////////////////////////////////////////////////////


#include <iostream>
#include <string.h>
#include <stdlib.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QUtil.hh>
#include <qpdf/QTC.hh>
#include <qpdf/Pl_StdioFile.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <jansson.h>
#include <assert.h>
using std::map;
using std::string;

void usage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " [options] file.pdf [password]\n";
    exit(1);
}

struct AnnotPair {
    json_t *urls;
    json_t *bookmarks;
};

json_t* processMediaBox(QPDFObjectHandle &media_box) {
    json_t *mediabox = NULL;
    if (media_box.isArray()) {
        mediabox = json_array();
        int count = media_box.getArrayNItems();
        for (int i=0; i < count; ++i) {
            QPDFObjectHandle handle (media_box.getArrayItem(i));
            std::string numeral = handle.unparseResolved();
            const char *numstr = numeral.c_str();
            const char * endptr = numstr + numeral.length();
            long int num = strtol(numstr, const_cast<char**>(&endptr), 10);
            if (numstr != endptr) {
                json_array_append_new(mediabox,json_integer(num));
            } else {
                json_decref(mediabox);
                return NULL;
            }
        }    
    }
    return mediabox;
}

AnnotPair getAnnotations(QPDFObjectHandle annots) {
    AnnotPair retval = {NULL, NULL};
    if (annots.isArray()) {
        int count = annots.getArrayNItems();
        for (int i=0; i < count; ++i) {
            QPDFObjectHandle annot (annots.getArrayItem(i));
            if (annot.isDictionary()) {
                std::map<std::string, QPDFObjectHandle> dict(annot.getDictAsMap());
                std::map<std::string, QPDFObjectHandle>::iterator rectangle, subtype;
                subtype = dict.find("/Subtype");
                rectangle = dict.find("/Rect");
                if (subtype != dict.end() 
                    && rectangle != dict.end()
                    && subtype->second.unparseResolved() == "/Link") {

                    json_t * rect = processMediaBox(rectangle->second);
                    if (rect!= NULL) {
                        map<string, QPDFObjectHandle>::iterator actionDict = dict.find("/A");
                        json_t ** target = &retval.bookmarks;
                        json_t *uri = NULL;
                        string data = annot.unparseResolved();
                        if (actionDict != dict.end()) {
                            if (actionDict->second.isDictionary()
                                && actionDict->second.hasKey("/URI")) {

                                target = &retval.urls;
                                string uri_s = actionDict->second.getKey("/URI").unparseResolved();
                                uri = json_string(uri_s.c_str());
                            }
                        }
                        std::map<std::string, QPDFObjectHandle>::iterator destDict 
                            = dict.find("/Dest");
                        if (destDict != dict.end()) {
                            if (destDict->second.isArray()) {
                                // <-- need to figure out how to reference page and x,y here
                            }else if (destDict->second.isDictionary()) {
                                //shouldn't get here
                            }else {
                                //shouldn't get here
                            }
                        }
                        if (*target == NULL) {
                            *target = json_array();
                        }
                        json_t *link = json_object();
                        json_object_set_new(link, "rect", rect);
                        json_object_set_new(link, "data", json_string(data.c_str()));
                        if (uri) {
                            json_object_set_new(link, "uri", uri);
                        }
                        json_array_append_new(*target, link);
                    }
                }
            }
        }
    }
    return retval;
}

json_t* fallbackMediaBox() {
    json_t *mediabox = json_array();
    fprintf(stderr, "Fall back to default mediabox\n");
    json_array_append_new(mediabox, json_integer(0));
    json_array_append_new(mediabox, json_integer(0));
    json_array_append_new(mediabox, json_integer(612));
    json_array_append_new(mediabox, json_integer(792));
    return mediabox;
}

json_t* processPage(const QPDFObjectHandle& page) {
    QPDFObjectHandle page_object (page);
    json_t *output = NULL;
    
    assert(page_object.isDictionary());
    std::map<std::string, QPDFObjectHandle> dict(page_object.getDictAsMap());
    std::map<std::string, QPDFObjectHandle>::iterator mb, annots = dict.find("/Annots");
    if (annots != dict.end()) {
        json_t *mediabox = NULL;
        mb = dict.find("/MediaBox");
        if (mb != dict.end()) {
            mediabox = processMediaBox(mb->second);
        }
        if (mediabox == NULL) {
            mediabox = fallbackMediaBox();
        }
        AnnotPair annot =  getAnnotations(annots->second);
        if (annot.urls != NULL) {
            output = json_object();
            json_object_set_new(output, "urls", annot.urls);
        }
        if (annot.bookmarks != NULL) {
            if (output == NULL) {
                output = json_object();
            }
            json_object_set_new(output, "bookmarks", annot.bookmarks);
        }
        if (output != NULL) {
            json_object_set_new(output, "mediabox", mediabox);
        }
    }
    return output;
}
int main(int argc, char** argv) {
    if (argc < 2) {
        usage(argv[0]);
    }
    bool pretty = false;
    if (strcmp(argv[1], "--pretty") == 0) {
        ++argv;
        --argc;
        pretty = true;
    }
    char const* filename = argv[1];
    char const* password = "";

    if (argc > 2) {
        password = argv[2];
    }

    try {
        QPDF qpdf;
        qpdf.processFile(filename, password);
        const std::vector<QPDFObjectHandle>&pages = qpdf.getAllPages();
        std::vector<QPDFObjectHandle>::const_iterator pageiter=pages.begin(),
            pageitere = qpdf.getAllPages().end();
        int page_number = 0;
        std::map<int, QPDFObjectHandle> page_map;
        //std::map<QPDFObjGen, int> ipage_map;

        for (;pageiter!=pageitere;++pageiter,++page_number) {
            page_map[page_number] = *pageiter;
            //ipage_map[pageiter->getObjGen()] = page_number;
        }
        pageiter = pages.begin();
        json_t *root = json_object();
        page_number = 0;
        for (;pageiter!=pageitere;++pageiter,++page_number) {
            json_t *page_structure = processPage(*pageiter);
            if (page_structure != NULL) {
                char page_number_s[16] = {0};
                sprintf(page_number_s, "%d", page_number);
                json_object_set_new(root, page_number_s, page_structure);
            }
        }
        char * json_data = json_dumps(root,
                                      pretty? JSON_INDENT(4)|JSON_ENSURE_ASCII|JSON_SORT_KEYS
                                      : (JSON_ENSURE_ASCII|JSON_COMPACT|JSON_SORT_KEYS));
        std::cout << json_data << std::endl;
        free(json_data);
        json_decref(root);
    } catch (std::exception &e) {
	    std::cerr << argv[0] << " processing file " << filename << ": " << e.what() << "\n";
        throw e;
    }

    return 0;
}
