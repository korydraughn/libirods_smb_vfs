#include "libirods_smb.h"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <memory>

#include <irods/objStat.h>
#include <irods/openCollection.h>
#include <irods/closeCollection.h>
#include <irods/readCollection.h>
#include <irods/collection.hpp>
#include <irods/miscUtil.h>
#include <irods/rmColl.h>

#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>

#include "irods_query.hpp"

namespace
{
    template <typename T,
              typename = std::enable_if_t<std::is_integral_v<T>>>
    class integral_bimap
    {
    public:
        using path_type     = std::string;
        using integral_type = T;

        bool map(const path_type& _absolute_path, integral_type _i)
        {
            if (auto iter = ints_.find(_absolute_path); iter != std::end(ints_))
                return false;

            if (auto iter = paths_.find(_i); iter != std::end(paths_))
                return false;

            ints_[_absolute_path] = _i;
            paths_[_i] = _absolute_path;

            return true;
        }

        integral_type insert(const path_type& _absolute_path)
        {
            if (auto iter = ints_.find(_absolute_path); iter != std::end(ints_))
                return iter->second;

            ints_[_absolute_path] = ++counter_;
            paths_[counter_] = _absolute_path;

            return counter_;
        }

        void erase(const path_type& _absolute_path)
        {
            if (auto iter = ints_.find(_absolute_path); iter != std::end(ints_))
            {
                ints_.erase(iter);
                paths_.erase(iter->second);
            }
        }

        integral_type integral(const path_type& _absolute_path) const
        {
            return ints_.at(_absolute_path);
        }

        const path_type& path(integral_type _i) const
        {
            return paths_.at(_i);
        }

    private:
        integral_type counter_{};
        // TODO Could replace these with a bimap of some type (maybe boost::bimap?).
        std::map<path_type, integral_type> ints_;
        std::map<integral_type, path_type> paths_;
    };

    auto get_root_path(const rodsEnv& _env) -> std::string;
    auto filename(const std::string& _path) -> std::string;
    auto list(rcComm_t* _conn, const std::string& _path) -> std::vector<std::string>;
}

struct irods_context_t
{
    rodsEnv env;
    rcComm_t* conn;
    std::string smb_path;
    std::string cwd;
    integral_bimap<std::int64_t> fsys;
    integral_bimap<std::int64_t> fd;
    std::unique_ptr<irods_collection_stream> dir;
    dirent dir_entry;
    error_code read_coll_ec;
};

auto ismb_test() -> error_code
{
    std::cout << "ismb_test :: fetching irods environment ...\n";

    rodsEnv env;
    auto status = getRodsEnv(&env);

    if (status < 0)
    {
        std::cout << "ismb_test :: environment retrieval error.\n";
        return 1;
    }

    std::cout << "ismb_test :: connecting to iRODS server ...\n";
    rErrMsg_t errors;
    auto* conn = rcConnect(env.rodsHost,
                           env.rodsPort,
                           env.rodsUserName,
                           env.rodsZone,
                           0,
                           &errors);

    if (!conn)
    {
        std::cout << "ismb_test :: connection error.\n";
        return 1;
    }

    std::cout << "ismb_test :: logging in ...\n";
    //status = clientLogin(conn);
    char password[] = "rods";
    status = clientLoginWithPassword(conn, password);

    if (status != 0)
    {
        std::cout << "ismb_test :: login error.\n";
        return 1;
    }

    std::cout << "ismb_test :: disconnecting ...\n";
    rcDisconnect(conn);

    std::cout << "ismb_test :: done.\n";

    return 0;
}

auto ismb_create_context(const char* _smb_path) -> irods_context*
{
    auto* ctx = new irods_context{};
    ctx->smb_path = boost::filesystem::path{_smb_path}.generic_string();
    std::cout << __func__ << " :: samba share path = " << _smb_path << '\n';
    return ctx;
}

auto ismb_destroy_context(irods_context* _ctx) -> void
{
    delete _ctx;
}

auto ismb_connect(irods_context* _ctx) -> error_code
{
    /*
    using namespace irods::experimental;

    static bool init_logging = true;

    if (init_logging)
    {
        init_logging = false;
        log::init();
        log::server::set_level(log::level::debug);
    }

    log::debug("loading environment ...");
    */

    //irods::dynamic_cast_hack();

    auto status = getRodsEnv(&_ctx->env);

    if (status < 0)
        return 1;

    //auto& api_tbl = irods::get_client_api_table();
    //auto& pk_tbl = irods::get_pack_table();
    //init_api_table(api_tbl, pk_tbl);

    //log::debug("connecting to iRODS server ...");

    rErrMsg_t errors;
    _ctx->conn = rcConnect(_ctx->env.rodsHost,
                           _ctx->env.rodsPort,
                           _ctx->env.rodsUserName,
                           _ctx->env.rodsZone,
                           0, //NO_RECONN,
                           &errors);

    if (!_ctx->conn)
        return 1;

    //log::debug("logging into iRODS server ...");

    //status = clientLogin(_ctx->conn);
    char password[] = "rods";
    status = clientLoginWithPassword(_ctx->conn, password);

    if (status != 0)
    {
        _ctx->conn = nullptr;
        return 1;
    }

    _ctx->cwd = get_root_path(_ctx->env);
    _ctx->fsys.insert(_ctx->cwd);

    //log::debug("login successful.");

    return 0;
}

auto ismb_disconnect(irods_context* _ctx) -> error_code
{
    //log::debug("disconnecting from iRODS server ...");
    rcDisconnect(_ctx->conn);
    //log::debug("disconnection successful.");
    return 0;
}

auto ismb_stat(irods_context* _ctx, const char* _path, irods_stat_info* _stat_info) -> error_code
{
    std::cout << __func__ << " :: _path (dirty)  = " << _path << '\n';

    namespace fs = boost::filesystem;

    std::string abs_path;

    if (!_path || std::strcmp(_path, ".") == 0)
    {
        abs_path = get_root_path(_ctx->env);
    }
    else if (boost::starts_with(_path, "./"))
    {
        abs_path = _path;
        boost::replace_first(abs_path, "./", _ctx->cwd + '/');
    }
    else if (boost::starts_with(_path, "/"))
    {
        abs_path = _path;
        boost::replace_first(abs_path, "/", get_root_path(_ctx->env) + '/');
    }
    else
    {
        abs_path = _ctx->cwd;
        abs_path += '/';
        abs_path += _path;
    }

    boost::replace_first(abs_path, _ctx->smb_path, ""); // Remove the samba share root.

    rodsObjStat_t* stat_info_ptr{};
    dataObjInp_t data_obj_input{};

    std::strncpy(data_obj_input.objPath, abs_path.c_str(), abs_path.length());
    std::cout << __func__ << " :: data_obj_input.objPath = " << data_obj_input.objPath << '\n';

    if (auto ec = rcObjStat(_ctx->conn, &data_obj_input, &stat_info_ptr); ec < 0)
        return ec;

    std::cout << std::boolalpha;
    std::cout << "found stat info: " << (stat_info_ptr != nullptr) << '\n';

    if (stat_info_ptr)
    {
#if 0
        std::cout << "\nstat results for [" << _path << "]\n";
        std::cout << "- object size = " << stat_info_ptr->objSize << '\n';
        std::cout << "- object type = " << stat_info_ptr->objType << '\n';
        std::cout << "- data mode   = " << stat_info_ptr->dataMode << '\n';
        std::cout << "- data id     = " << stat_info_ptr->dataId << '\n';
        std::cout << "- checksum    = " << stat_info_ptr->chksum << '\n';
        std::cout << "- owner name  = " << stat_info_ptr->ownerName << '\n';
        std::cout << "- owner zone  = " << stat_info_ptr->ownerZone << '\n';
        std::cout << "- create time = " << stat_info_ptr->createTime << '\n';
        std::cout << "- modify time = " << stat_info_ptr->modifyTime << '\n';
        std::cout << "- resc. hier  = " << stat_info_ptr->rescHier << '\n';
        std::cout << '\n';
#endif

        _ctx->fsys.insert(data_obj_input.objPath);

        std::memset(_stat_info, 0, sizeof(irods_stat_info));

        _stat_info->size = stat_info_ptr->objSize;
        _stat_info->type = stat_info_ptr->objType;
        _stat_info->mode = static_cast<int>(stat_info_ptr->dataMode);
        _stat_info->id = _ctx->fsys.insert(data_obj_input.objPath);
        std::strncpy(_stat_info->owner_name, stat_info_ptr->ownerName, strlen(stat_info_ptr->ownerName));
        std::strncpy(_stat_info->owner_zone, stat_info_ptr->ownerZone, strlen(stat_info_ptr->ownerZone));
        _stat_info->creation_time = std::stoll(stat_info_ptr->createTime);
        _stat_info->modified_time = std::stoll(stat_info_ptr->modifyTime);

        freeRodsObjStat(stat_info_ptr);
    }

    return 0;
}

auto ismb_list(irods_context* _ctx, const char* _path, irods_string_array* _entries) -> void
{
    std::cout << __func__ << " :: _path = " << _path << '\n';
    
    auto entries = list(_ctx->conn, _path);

    if (entries.empty())
        return;

    _entries->strings = new irods_char_array[entries.size()];
    _entries->size = static_cast<long>(entries.size());
    
    using size_type = std::vector<std::string>::size_type;

    for (size_type i = 0; i < entries.size(); ++i)
    {
        auto& entry = _entries->strings[i];
        entry.data = new char[entries[i].length() + 1] {};
        entry.length = entries[i].length();
        std::strncpy(entry.data, entries[i].c_str(), entries[i].length());
    }
}

auto ismb_free_string_array(irods_string_array* _string_array) -> void
{
    using index_type = decltype(_string_array->size);

    for (index_type i = 0; i < _string_array->size; ++i)
        delete[] _string_array->strings[i].data;

    delete[] _string_array->strings;
}

auto ismb_free_string(const char* _string) -> void
{
    delete[] _string;
}

auto ismb_chdir(irods_context* _ctx, const char* _target_dir) -> error_code
{
    std::cout << __func__ << " :: _target_dir    = " << _target_dir << '\n';
    std::cout << __func__ << " :: _ctx->smb_path = " << _ctx->smb_path << '\n';

    using namespace std::string_literals;

    if (_target_dir == "/"s || _target_dir == _ctx->smb_path)
    {
        _ctx->cwd = get_root_path(_ctx->env);
        return 0;
    }

    // Verify that the directory exists.

    namespace fs = boost::filesystem;

    const auto cwd = (fs::path{_ctx->cwd} / _target_dir).generic_string();

    std::cout << __func__ << " :: possible new working directory = " << cwd << '\n';

    auto sql = "select count(COLL_NAME) where COLL_NAME = '"s;
    sql += cwd;
    sql += "'";

    for (const auto& row : irods::query{_ctx->conn, sql})
    {
        for (const auto& value : row)
        {
            if (value == "1")
            {
                _ctx->cwd = cwd;
                std::cout << __func__ << " :: new working directory = " << _ctx->cwd << '\n';
                return 0;
            }
        }
    }

    std::cout << __func__ << " :: invalid directory.\n";

    return -1;
}

auto ismb_getwd(irods_context* _ctx, char** _dir) -> void
{
    *_dir = new char[_ctx->cwd.length() + 1] {};
    std::strncpy(*_dir, _ctx->cwd.c_str(), _ctx->cwd.length());
}

auto ismb_opendir(irods_context* _ctx,
                  const char* _path,
                  irods_collection_stream** _coll_stream) -> error_code
{
    std::cout << __func__ << " :: _path = " << _path << '\n';

    std::string path;

    if (!_path || std::strcmp(_path, ".") == 0)
    {
        path = _ctx->cwd;
    }
    else if (boost::starts_with(_path, "./"))
    {
        path = _path;
        boost::replace_first(path, "./", _ctx->cwd + '/');
    }
    else if (boost::starts_with(_path, "/"))
    {
        path = _path;
        boost::replace_first(path, "/", get_root_path(_ctx->env) + '/');
    }
    else
    {
        path = _ctx->cwd;
        path += '/';
        path += _path;
    }

    std::cout << __func__ << " :: path  = " << path << '\n';

    //path.erase(path.find_last_of("/."));
    //path.erase(path.find_last_of("/.."));
    while ('/' == path.back() || '.' == path.back())
        path.erase(path.length() - 1);

    std::cout << __func__ << " :: path  = " << path << '\n';

    collInp_t coll_input{};
    coll_input.flags = LONG_METADATA_FG;
    std::strncpy(coll_input.collName, path.c_str(), path.length());

    auto handle = rcOpenCollection(_ctx->conn, &coll_input);

    if (handle < 0)
    {
        std::cout << __func__ << " :: failed to open collection.\n";
        return -1;
    }

    _ctx->dir.reset(new irods_collection_stream{handle});
    *_coll_stream = _ctx->dir.get();

    return 0;
}

auto ismb_fdopendir(irods_context* _ctx,
                    const char* _path,
                    irods_collection_stream** _coll_stream) -> error_code
{
    return ismb_opendir(_ctx, _path, _coll_stream);
}

auto ismb_readdir(irods_context* _ctx, irods_collection_stream* _coll_stream) -> dirent*
{
    collEnt_t* coll_entry{};

    _ctx->read_coll_ec = rcReadCollection(_ctx->conn, *_coll_stream, &coll_entry);

    if (_ctx->read_coll_ec < 0)
    {
        return nullptr;
    }

    _ctx->dir_entry = {}; // Clear old data.

    if (coll_entry->objType == DATA_OBJ_T)
    {
        const auto length = std::strlen(coll_entry->dataName);
        std::strncpy(_ctx->dir_entry.d_name, coll_entry->dataName, length);
    }
    else if (coll_entry->objType == COLL_OBJ_T)
    {
        std::string name = coll_entry->collName;

        if (auto pos = name.find_last_of('/'); pos != std::string::npos)
            name.erase(0, pos + 1);

        std::strncpy(_ctx->dir_entry.d_name, name.c_str(), name.length());
    }

    _ctx->dir_entry.d_ino = _ctx->fsys.insert(_ctx->dir_entry.d_name);

    freeCollEnt(coll_entry);

    return &_ctx->dir_entry;
}

error_code ismb_seekdir(irods_context* _ctx, const char* _path)
{
    return 0;
}

error_code ismb_telldir(irods_context* _ctx)
{
    return _ctx->read_coll_ec >= 0 ? 0 : -1;
}

error_code ismb_rewind_dir(irods_context* _ctx, const char* _path)
{
    return 0;
}

error_code ismb_mkdir(irods_context* _ctx, const char* _path)
{
    std::cout << __func__ << " :: _path = " << _path << '\n';

    auto abs_path = _ctx->cwd;
    abs_path += '/';
    abs_path += _path;

    std::cout << __func__ << " :: abs_path = " << abs_path << '\n';

    char coll_path[1024]{};
    std::strncpy(coll_path, abs_path.c_str(), abs_path.length());

    if (auto ec = mkColl(_ctx->conn, coll_path); ec != 0)
    {
        std::cout << __func__ << " :: mkColl() failed.\n";
        return -1;
    }

    _ctx->fsys.insert(abs_path);

    return 0;
}

error_code ismb_rmdir(irods_context* _ctx, const char* _path)
{
    std::cout << __func__ << " :: _path = " << _path << '\n';

    auto abs_path = _ctx->cwd;
    abs_path += '/';
    abs_path += _path;

    std::cout << __func__ << " :: abs_path = " << abs_path << '\n';

    char coll_path[1024]{};
    std::strncpy(coll_path, abs_path.c_str(), abs_path.length());

    collInp_t coll_input{};
    std::strncpy(coll_input.collName, abs_path.c_str(), abs_path.length());

    //addKeyVal(&coll_input.condInput, FORCE_FLAG_KW, "");
    //addKeyVal(&coll_input.condInput, RECURSIVE_OPR__KW, "");

    constexpr int verbose = 0;
    if (auto ec = rcRmColl(_ctx->conn, &coll_input, verbose); ec < 0)
    {
        std::cout << __func__ << " :: rcRmColl() failed.\n";
        return -1;
    }

    _ctx->fsys.erase(abs_path);
    std::cout << __func__ << " :: collection removed.\n";

    return 0;
}

void ismb_closedir(irods_context* _ctx, irods_collection_stream* _coll_stream)
{
    rcCloseCollection(_ctx->conn, *_coll_stream);
    _ctx->dir.reset();
}

//
// File Operations
//

auto ismb_open(irods_context* _ctx, const char* _filename, int _flags, int _mode) -> int
{
    namespace fs = boost::filesystem;

    std::cout << __func__ << " :: _filename = " << _filename << '\n';
    std::cout << __func__ << " :: _flags    = " << _flags << '\n';
    std::cout << __func__ << " :: _mode     = " << _mode << '\n';

    dataObjInp_t args{};

    args.createMode = _mode;
    args.openFlags = _flags;

    auto abs_path = _ctx->cwd;
    abs_path += '/';
    abs_path += fs::path{_filename}.filename().generic_string();
    std::cout << __func__ << " :: abs_path  = " << abs_path << '\n';
    rstrcpy(args.objPath, abs_path.c_str(), MAX_NAME_LEN);

    addKeyVal(&args.condInput, RESC_NAME_KW, _ctx->env.rodsDefResource);
    std::cout << __func__ << " :: def. resc = " << _ctx->env.rodsDefResource << '\n';

    if (const auto fd = rcDataObjOpen(_ctx->conn, &args); fd >= 0)
    {
        _ctx->fd.map(abs_path, fd);
        return fd;
    }

    return -1;
}

auto ismb_close(irods_context* _ctx, int _fd) -> int
{
    openedDataObjInp_t args{};

    args.l1descInx = _fd;

    if (rcDataObjClose(_ctx->conn, &args) < 0)
        return -1;

    return 0;
}

auto ismb_write(irods_context* _ctx, int _fd, void* _buffer, int _buffer_size) -> int
{
    openedDataObjInp_t obj_args{};
    obj_args.l1descInx = _fd;

    bytesBuf_t buf_args{};
    buf_args.buf = _buffer;
    buf_args.len = _buffer_size;

    return rcDataObjWrite(_ctx->conn, &obj_args, &buf_args);
}

auto ismb_fstat(irods_context* _ctx, int _fd, irods_stat_info* _stat_info) -> error_code
{
    namespace fs = boost::filesystem;
    const auto filename = fs::path{_ctx->fd.path(_fd)}.filename().generic_string();
    return ismb_stat(_ctx, filename.c_str(), _stat_info);
}

namespace
{
    auto get_root_path(const rodsEnv& _env) -> std::string
    {
        std::string root = "/";
        root += _env.rodsZone;
        root += "/home/";
        root += _env.rodsUserName;

        return root;
    }

    auto filename(const std::string& _path) -> std::string
    {
        return boost::filesystem::path{_path}.filename().generic_string();
    }

    auto list(rcComm_t* _conn, const std::string& _path) -> std::vector<std::string>
    {
        using namespace std::string_literals;

        std::vector<std::string> entries;

        for (const auto& sql : {"select DATA_NAME where DATA_NAME = '"s + filename(_path) + "'",
                                "select DATA_NAME where COLL_NAME = '"s + _path + "'",
                                "select COLL_NAME where COLL_NAME like '"s + _path + "%'"})
        {
            for (const auto& row : irods::query{_conn, sql})
                for (const auto& value : row)
                    if (_path != value)
                        entries.push_back(filename(value));
        }

        return entries;
    }
}

