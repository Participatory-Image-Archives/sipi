/*
 * Copyright © 2016 Lukas Rosenthaler, Andrea Bianco, Benjamin Geer,
 * Ivan Subotic, Tobias Schweizer, André Kilchenmann, and André Fatton.
 * This file is part of Sipi.
 * Sipi is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * Sipi is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * Additional permission under GNU AGPL version 3 section 7:
 * If you modify this Program, or any covered work, by linking or combining
 * it with Kakadu (or a modified version of that library) or Adobe ICC Color
 * Profiles (or a modified version of that library) or both, containing parts
 * covered by the terms of the Kakadu Software Licence or Adobe Software Licence,
 * or both, the licensors of this Program grant you additional permission
 * to convey the resulting work.
 * See the GNU Affero General Public License for more details.
 * You should have received a copy of the GNU Affero General Public
 * License along with Sipi.  If not, see <http://www.gnu.org/licenses/>.
 */
/*!
 * This file implements a Webserver using mongoose (See \url https://github.com/cesanta/mongoose)
 *
 * {scheme}://{server}{/prefix}/{identifier}/{region}/{size}/{rotation}/{quality}.{format}
 *
 * We support cross domain scripting (CORS according to \url http://www.html5rocks.com/en/tutorials/cors/)
 */
#ifndef __defined_sipihttp_server_h
#define __defined_sipihttp_server_h

#include <string>
#include <sys/types.h>
#include <unistd.h>

#include "Server.h"
#include "iiifparser/SipiRotation.h"
#include "iiifparser/SipiQualityFormat.h"
#include "SipiCache.h"

#include "lua.hpp"


namespace Sipi {

   /*!
    * The class SipiHttpServer implements a webserver that can be used to serve images using the IIIF
    * API. For details on the API look for  \url http://iiif.io . I implemented support for
    * cross domain scripting (CORS according to \url http://www.html5rocks.com/en/tutorials/cors/). As a
    * special feature we support acces to the old PHP-based salsah version (this is a bad hack!)
    */
    class SipiHttpServer : public shttps::Server {
    private:
    protected:
        pid_t _pid;
        std::string _imgroot;
        std::string _salsah_prefix;
        bool _prefix_as_path;
        std::string _logfile;
        SipiCache *_cache;
    public:
       /*!
        * Constructor which automatically starts the server
        *
        * \param port_p Portnumber on which the server should listen
        * \param root_p Path to the root of directory containing the images
        */
        SipiHttpServer(int port_p, unsigned nthreads_p = 4, const std::string userid_str = "", const std::string &logfile_p = "sipi.log");
        ~SipiHttpServer();
        void run();

        std::pair<std::string,std::string> get_canonical_url(int img_w, int img_h, const std::string &host, const std::string &prefix, const std::string &identifier, SipiRegion &region, SipiSize &size, SipiRotation &rotation, SipiQualityFormat &quality_format);


        inline pid_t pid(void) { return _pid; }

        inline void imgroot(const std::string &imgroot_p) { _imgroot = imgroot_p; }
        inline std::string imgroot(void) { return _imgroot; }

        inline std::string salsah_prefix(void) {return _salsah_prefix; }
        inline void salsah_prefix(const std::string &salsah_prefix) { _salsah_prefix = salsah_prefix; }

        inline bool prefix_as_path(void) { return _prefix_as_path; }
        inline void prefix_as_path(bool prefix_as_path_p) { _prefix_as_path = prefix_as_path_p; }

        void cache(const std::string &cachedir_p, long long max_cachesize_p = 0, unsigned max_nfiles_p = 0, float cache_hysteresis_p = 0.1);
        inline SipiCache *cache() { return _cache; }

    };

}

#endif
