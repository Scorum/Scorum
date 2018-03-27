/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <vector>
#include <set>
#include <sstream>

#include <fc/io/json.hpp>
#include <fc/io/stdio.hpp>
#include <fc/network/http/server.hpp>
#include <fc/network/http/websocket.hpp>
#include <fc/rpc/http_api.hpp>
#include <fc/rpc/websocket_api.hpp>
#include <fc/smart_ref_impl.hpp>

#include <graphene/utilities/key_conversion.hpp>

#include <scorum/app/api.hpp>
#include <scorum/protocol/protocol.hpp>
#include <scorum/wallet/wallet.hpp>
#include <scorum/chain/genesis/genesis_state.hpp>
#include <scorum/egenesis/egenesis.hpp>
#include <scorum/cli/formatter.hpp>

#include <fc/interprocess/signals.hpp>
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>

#include <fc/log/console_appender.hpp>
#include <fc/log/file_appender.hpp>
#include <fc/log/logger.hpp>
#include <fc/log/logger_config.hpp>

#include "wallet_app.hpp"

#ifdef WIN32
#include <signal.h>
#else
#include <csignal>
#endif

using namespace scorum;
namespace bpo = boost::program_options;

void set_new_password(std::shared_ptr<wallet_app>& wallet_cli,
                      std::shared_ptr<wallet::wallet_api>& wapiptr,
                      bool show_asterisk)
{
    std::string psw = wallet_cli->get_secret("New password:", show_asterisk);
    if (psw == wallet_cli->get_secret("Submit password:", show_asterisk))
        wapiptr->set_password_internal(psw);
}

int main(int argc, char** argv)
{
    try
    {

        boost::program_options::options_description opts;
        // clang-format off
        opts.add_options()
                ("help,h", "Print this help message and exit.")
                ("version,v", "Print version number and exit.")
                ("server-rpc-endpoint,s", bpo::value<std::string>()->implicit_value("ws://127.0.0.1:8090"), "Server websocket RPC endpoint")
                ("server-rpc-user,u",     bpo::value<std::string>(), "Server Username")
                ("server-rpc-password,p", bpo::value<std::string>(), "Server Password")
                ("cert-authority,a",      bpo::value<std::string>()->default_value("_default"),        "Trusted CA bundle file for connecting to wss:// TLS server")
                ("rpc-endpoint,r",        bpo::value<std::string>()->implicit_value("127.0.0.1:8091"), "Endpoint for wallet websocket RPC to listen on")
                ("rpc-tls-endpoint,t",    bpo::value<std::string>()->implicit_value("127.0.0.1:8092"), "Endpoint for wallet websocket TLS RPC to listen on")
                ("rpc-tls-certificate,c", bpo::value<std::string>()->implicit_value("server.pem"),     "PEM certificate for wallet websocket TLS RPC")
                ("rpc-http-endpoint,H",   bpo::value<std::string>()->implicit_value("127.0.0.1:8093"), "Endpoint for wallet HTTP RPC to listen on")
                ("daemon,d", "Run the wallet in daemon mode")
                ("rpc-http-allowip",      bpo::value<std::vector<std::string>>()->multitoken(),        "Allows only specified IPs to connect to the HTTP endpoint")
                ("wallet-file,w",         bpo::value<std::string>()->default_value("wallet.json"),     "Wallet configuration to load")
                ("chain-id",              bpo::value<std::string>(),                                   "Chain ID to connect to")
                ("suggest-brain-key",     "Suggest brain key")
                ("change-password,P",     "Change password")
                ("import-key,I",          "Import key")
                ("list-keys,l",           "List loaded keys")
                ("show-asterisk",         "Show asterisk while secret input")
                ("strict,S",              "Switch on strict mode for API (forbid functions for secret menegment, e.g. set_password, import-key)");
        // clang-format on

        std::vector<std::string> allowed_ips;

        bpo::variables_map options;

        bpo::store(bpo::parse_command_line(argc, argv, opts), options);

        if ((options.count("suggest-brain-key") + options.count("change-password") + options.count("import-key")
             + options.count("list-keys"))
            > 1)
        {
            std::cerr << "Сonflicting options are passed\n";
            return 1;
        }

        if (options.count("version"))
        {
            app::print_application_version();
            return 0;
        }

        if (options.count("help"))
        {
            std::cout << opts << "\n";
            return 0;
        }

        if (options.count("suggest-brain-key"))
        {
            std::cout << fc::json::to_string(scorum::wallet::suggest_brain_key()) << "\n";
            return 0;
        }

        if (options.count("rpc-http-allowip") && options.count("rpc-http-endpoint"))
        {
            allowed_ips = options["rpc-http-allowip"].as<std::vector<std::string>>();
            wdump((allowed_ips));
        }

        fc::path data_dir;
        fc::logging_config cfg;
        fc::path log_dir = data_dir / "logs";

        fc::file_appender::config ac;
        ac.filename = log_dir / "rpc" / "rpc.log";
        ac.flush = true;
        ac.rotate = true;
        ac.rotation_interval = fc::hours(1);
        ac.rotation_limit = fc::days(1);

        std::cout << "Logging RPC to file: " << ac.filename.string() << std::endl;

        cfg.appenders.push_back(fc::appender_config("default", "console", fc::variant(fc::console_appender::config())));
        cfg.appenders.push_back(fc::appender_config("rpc", "file", fc::variant(ac)));

        cfg.loggers = { fc::logger_config("default"), fc::logger_config("rpc") };
        cfg.loggers.front().level = fc::log_level::info;
        cfg.loggers.front().appenders = { "default" };
        cfg.loggers.back().level = fc::log_level::debug;
        cfg.loggers.back().appenders = { "rpc" };

        //
        // TODO:  We read wallet_data twice, once in main() to grab the
        //    socket info, again in wallet_api when we do
        //    load_wallet_file().  Seems like this could be better
        //    designed.
        //
        wallet::wallet_data wdata;

        fc::path wallet_file(options.count("wallet-file") ? options.at("wallet-file").as<std::string>()
                                                          : "wallet.json");

        std::cout << "Wallet file: " << wallet_file.string() << std::endl;

        if (fc::exists(wallet_file))
        {
            wdata = fc::json::from_file(wallet_file).as<wallet::wallet_data>();
        }
        else
        {
            if (options.count("chain-id"))
            {
                wdata.chain_id = protocol::chain_id_type(options.at("chain-id").as<std::string>());
                std::cout << "Starting a new wallet with chain ID " << wdata.chain_id.str() << " (from CLI)\n";
            }
            else
            {
                wdata.chain_id = egenesis::get_egenesis_chain_id();
                std::cout << "Starting a new wallet with chain ID " << wdata.chain_id.str() << " (from egenesis)\n";
            }
        }

        // but allow CLI to override
        if (options.count("server-rpc-endpoint"))
            wdata.ws_server = options.at("server-rpc-endpoint").as<std::string>();
        if (options.count("server-rpc-user"))
            wdata.ws_user = options.at("server-rpc-user").as<std::string>();
        if (options.count("server-rpc-password"))
            wdata.ws_password = options.at("server-rpc-password").as<std::string>();

        fc::http::websocket_client client(options["cert-authority"].as<std::string>());
        idump((wdata.ws_server));
        auto con = client.connect(wdata.ws_server);
        auto apic = std::make_shared<fc::rpc::websocket_api_connection>(*con);

        auto wapiptr = std::make_shared<wallet::wallet_api>(wdata, options.count("strict"));
        auto wallet_cli = std::make_shared<wallet_app>();

        wapiptr->set_wallet_filename(wallet_file.generic_string());
        wapiptr->load_wallet_file();

        const std::string psw_prompt = "\nLogin:";

        if (options.count("list-keys"))
        {
            wapiptr->unlock_internal(wallet_cli->get_secret(psw_prompt, options.count("show-asterisk")));
            std::cout << fc::json::to_string(wapiptr->list_keys()) << "\n";
            return 0;
        }

        if (options.count("change-password"))
        {
            wapiptr->unlock_internal(wallet_cli->get_secret(psw_prompt, options.count("show-asterisk")));
            set_new_password(wallet_cli, wapiptr, options.count("show-asterisk"));
            return 0;
        }

        if (options.count("import-key"))
        {
            wapiptr->unlock_internal(wallet_cli->get_secret(psw_prompt, options.count("show-asterisk")));
            wapiptr->import_key_internal(wallet_cli->get_secret("WIF:", options.count("show-asterisk")));
            return 0;
        }

        auto remote_api = apic->get_remote_api<login_api>(1);
        edump((wdata.ws_user)(wdata.ws_password));
        // TODO:  Error message here
        FC_ASSERT(remote_api->login(wdata.ws_user, wdata.ws_password));

        wapiptr->connect(remote_api);

        if (wapiptr->is_new())
        {
            set_new_password(wallet_cli, wapiptr, options.count("show-asterisk"));
        }

        wapiptr->unlock_internal(wallet_cli->get_secret(psw_prompt, options.count("show-asterisk")));

        fc::api<wallet::wallet_api> wapi(wapiptr);

        enum class wallet_state_type
        {
            no_password,
            locked,
            unlocked
        };
        static const std::map<wallet_state_type, std::string> wallet_states
            = { { wallet_state_type::no_password, "(!)" },
                { wallet_state_type::locked, "(!)" },
                { wallet_state_type::unlocked, options.count("strict") ? ("") : ("---") } };

        auto promptFormatter = [](const std::string& state = "") -> std::string {
            static const std::string prompt = ">>> ";
            std::stringstream out;
            out << state << " " << prompt;
            return out.str();
        };

        for (auto& name_formatter : wapiptr->get_result_formatters())
            wallet_cli->format_result(name_formatter.first, name_formatter.second);

        boost::signals2::scoped_connection closed_connection(con->closed.connect([=] {
            std::cerr << "Server has disconnected us.\n";
            wallet_cli->stop();
        }));
        (void)(closed_connection);

        if (wapiptr->is_new())
        {
            std::cout << "Please use the set_password method to initialize a new wallet before continuing\n";
            wallet_cli->set_prompt(promptFormatter(wallet_states.at(wallet_state_type::no_password)));
        }

        wallet_cli->set_prompt(promptFormatter(wallet_states.at(wallet_state_type::unlocked)));

        boost::signals2::scoped_connection locked_connection(wapiptr->lock_changed.connect([&](bool locked) {
            if (locked)
            {
                wallet_cli->set_prompt(promptFormatter(wallet_states.at(wallet_state_type::locked)));
            }
            else
            {
                wallet_cli->set_prompt(promptFormatter(wallet_states.at(wallet_state_type::unlocked)));
            }
        }));

        auto _websocket_server = std::make_shared<fc::http::websocket_server>();
        if (options.count("rpc-endpoint"))
        {
            _websocket_server->on_connection([&](const fc::http::websocket_connection_ptr& c) {
                std::cout << "here... \n";
                wlog(".");
                auto wsc = std::make_shared<fc::rpc::websocket_api_connection>(*c);
                wsc->register_api(wapi);
                c->set_session_data(wsc);
            });
            ilog("Listening for incoming RPC requests on ${p}", ("p", options.at("rpc-endpoint").as<std::string>()));
            _websocket_server->listen(fc::ip::endpoint::from_string(options.at("rpc-endpoint").as<std::string>()));
            _websocket_server->start_accept();
        }

        std::string cert_pem = "server.pem";
        if (options.count("rpc-tls-certificate"))
            cert_pem = options.at("rpc-tls-certificate").as<std::string>();

        auto _websocket_tls_server = std::make_shared<fc::http::websocket_tls_server>(cert_pem);
        if (options.count("rpc-tls-endpoint"))
        {
            _websocket_tls_server->on_connection([&](const fc::http::websocket_connection_ptr& c) {
                auto wsc = std::make_shared<fc::rpc::websocket_api_connection>(*c);
                wsc->register_api(wapi);
                c->set_session_data(wsc);
            });
            ilog("Listening for incoming TLS RPC requests on ${p}",
                 ("p", options.at("rpc-tls-endpoint").as<std::string>()));
            _websocket_tls_server->listen(
                fc::ip::endpoint::from_string(options.at("rpc-tls-endpoint").as<std::string>()));
            _websocket_tls_server->start_accept();
        }

        std::set<fc::ip::address> allowed_ip_set;

        auto _http_server = std::make_shared<fc::http::server>();
        if (options.count("rpc-http-endpoint"))
        {
            ilog("Listening for incoming HTTP RPC requests on ${p}",
                 ("p", options.at("rpc-http-endpoint").as<std::string>()));
            for (const auto& ip : allowed_ips)
                allowed_ip_set.insert(fc::ip::address(ip));

            _http_server->listen(fc::ip::endpoint::from_string(options.at("rpc-http-endpoint").as<std::string>()));
            //
            // due to implementation, on_request() must come AFTER listen()
            //
            _http_server->on_request([&](const fc::http::request& req, const fc::http::server::response& resp) {
                auto itr = allowed_ip_set.find(fc::ip::endpoint::from_string(req.remote_endpoint).get_address());
                if (itr == allowed_ip_set.end())
                {
                    elog("rejected connection from ${ip} because it isn't in allowed set ${s}",
                         ("ip", req.remote_endpoint)("s", allowed_ip_set));
                    resp.set_status(fc::http::reply::NotAuthorized);
                    return;
                }
                std::shared_ptr<fc::rpc::http_api_connection> conn = std::make_shared<fc::rpc::http_api_connection>();
                conn->register_api(wapi);
                conn->on_request(req, resp);
            });
        }

        if (!options.count("daemon"))
        {
            wallet_cli->register_api(wapi);
            wapiptr->set_exit_func([wallet_cli]() {
                FC_ASSERT(wallet_cli);
                wallet_cli->stop();
            });
            std::cout << std::endl;
            wallet_cli->start();
            wallet_cli->wait();
        }
        else
        {
            fc::promise<int>::ptr exit_promise = new fc::promise<int>("UNIX Signal Handler");
            fc::set_signal_handler([&exit_promise](int signal) { exit_promise->set_value(signal); }, SIGINT);

            ilog("Entering Daemon Mode, ^C to exit");
            exit_promise->wait();
        }

        wapi->save_wallet_file(wallet_file.generic_string());
        locked_connection.disconnect();
        closed_connection.disconnect();
    }
    catch (const fc::exception& e)
    {
        std::cerr << e.to_detail_string() << "\n";
        return -1;
    }
    return 0;
}
