#define DUCKDB_EXTENSION_MAIN

#include "prql_extension.hpp"

#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/statement/extension_statement.hpp"

extern "C" {
  #include "libprql_lib.h"
}

namespace duckdb {

static void LoadInternal(DatabaseInstance &instance) {
  auto &config = DBConfig::GetConfig(instance);
  PrqlParserExtension prql_parser;
  config.parser_extensions.push_back(prql_parser);
  config.operator_extensions.push_back(make_unique<PrqlOperatorExtension>());
}

void PrqlExtension::Load(DuckDB &db) { LoadInternal(*db.instance); }

ParserExtensionParseResult prql_parse(ParserExtensionInfo *,
                                      const std::string &query) {
  // buffer length should not be less than 1K because we may get an error
  // from the PRQL compiler with a very short query
  int buffer_length = 1024;
  // allocate a buffer 3 times the length of the PRQL query to store the
  // generated SQL query
  if (buffer_length < query.length() * 3) {
    buffer_length = query.length() * 3;
  }
  std::vector<char> buffer;
  buffer.reserve(buffer_length);

  // TODO: support multiple statements? Requires parser to split on ; (if it's
  // not part of a comment or string or ...)

  // remove trailing semicolon
  string trimmed_string = query;
  if (trimmed_string[trimmed_string.length() - 1] == ';') {
    trimmed_string.pop_back();
  }

  string header = string("prql target:sql.duckdb\n");
  trimmed_string.insert(0, header);

  // run prql -> sql conversion
  Options options;
  options.format = false;
  options.target = const_cast<char*>("sql.duckdb");
  options.signature_comment = false;
  int conversion_status = compile(trimmed_string.c_str(), &options, buffer.data());
  // printf("%s", buf);
  auto sql_query_or_error = string(buffer.data());
  buffer.clear();

  if (conversion_status != 0) {
    // sql_query_or_error contains error string
    // TODO: decide when to consider it a PRQL failure vs this parser extension
    //  not being responsible for parsing the statement.
    //  For now, we consider any malformed statement a PRQL one.
    //  In the future we can try using a couple of basic heuristics to classify
    //  (e.g. only those queries starting with "from"),
    //  or we could mandate that PRQL queries in duckdb are always prefixed with
    //  a certain string / symbol, e.g. |>, and then remove that here.
    return ParserExtensionParseResult(move(sql_query_or_error));
  }

  Parser parser; // TODO Pass (ClientContext.GetParserOptions());
  parser.ParseQuery(move(sql_query_or_error));
  vector<unique_ptr<SQLStatement>> statements = std::move(parser.statements);

  return ParserExtensionParseResult(
      make_unique_base<ParserExtensionParseData, PrqlParseData>(
          move(statements[0])));
}

ParserExtensionPlanResult
prql_plan(ParserExtensionInfo *, ClientContext &context,
          unique_ptr<ParserExtensionParseData> parse_data) {
  // We stash away the ParserExtensionParseData before throwing an exception
  // here. This allows the planning to be picked up by prql_bind instead, but
  // we're not losing important context.
  auto prql_state = make_shared<PrqlState>(move(parse_data));
  context.registered_state["prql"] = prql_state;
  throw BinderException("Use prql_bind instead");
}

BoundStatement prql_bind(ClientContext &context, Binder &binder,
                         OperatorExtensionInfo *info, SQLStatement &statement) {
  switch (statement.type) {
  case StatementType::EXTENSION_STATEMENT: {
    auto &extension_statement = dynamic_cast<ExtensionStatement &>(statement);
    if (extension_statement.extension.parse_function == prql_parse) {
      auto lookup = context.registered_state.find("prql");
      if (lookup != context.registered_state.end()) {
        auto prql_state = (PrqlState *)lookup->second.get();
        auto prql_binder = Binder::CreateBinder(context);
        auto prql_parse_data =
            dynamic_cast<PrqlParseData *>(prql_state->parse_data.get());
        return prql_binder->Bind(*(prql_parse_data->statement));
      }
      throw BinderException("Registered state not found");
    }
  }
  default:
    // No-op empty
    return {};
  }
}

} // namespace duckdb

extern "C" {

DUCKDB_EXTENSION_API void prql_init(duckdb::DatabaseInstance &db) {
  LoadInternal(db);
}

DUCKDB_EXTENSION_API const char *prql_version() {
  return duckdb::DuckDB::LibraryVersion();
}
}

#ifndef DUCKDB_EXTENSION_MAIN
#error DUCKDB_EXTENSION_MAIN not defined
#endif
