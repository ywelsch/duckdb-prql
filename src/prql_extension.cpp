#define DUCKDB_EXTENSION_MAIN

#include "prql_extension.hpp"

#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/statement/extension_statement.hpp"

#include "prqlc.hpp"

#include "re2/re2.h"

#include <sstream>

namespace duckdb {

static void LoadInternal(DatabaseInstance &instance) {
  auto &config = DBConfig::GetConfig(instance);
  PrqlParserExtension prql_parser;
  config.parser_extensions.push_back(prql_parser);
  config.operator_extensions.push_back(make_uniq<PrqlOperatorExtension>());
}

void PrqlExtension::Load(DuckDB &db) { LoadInternal(*db.instance); }

void transform_block(const std::string &block, std::stringstream &ss,
                     bool &failed) {
  prqlc::Options options;
  options.format = false;
  options.target = const_cast<char *>("sql.duckdb");
  options.signature_comment = false;
  prqlc::CompileResult compile_result = compile(block.c_str(), &options);

  for (int i = 0; i < compile_result.messages_len; i++) {
    prqlc::Message const *e = &compile_result.messages[i];
    if (e->kind == prqlc::MessageKind::Error) {
      if (failed) {
        // append new line for next failure
        ss << "\n";
      }
      if (e->display != NULL) {
        ss << *e->display;
      } else if (e->code != NULL) {
        ss << "[" << *e->code << "] Error: " << e->reason;
      } else {
        ss << "Error: " << e->reason;
      }
      failed = true;
    }
  }
  if (!failed) {
    ss << compile_result.output;
  }
  prqlc::result_destroy(compile_result);
}

ParserExtensionParseResult prql_parse(ParserExtensionInfo *,
                                      const std::string &query) {
  // TODO: support multiple statements? Requires parser to split on ; (if it's
  // not part of a comment or string or ...)

  // remove trailing semicolon
  string trimmed_string = query;
  if (trimmed_string[trimmed_string.length() - 1] == ';') {
    trimmed_string.pop_back();
  }

  // Identify PRQL blocks, delimited by "(|" and "|)"
  RE2::Options options;
  options.set_dot_nl(true);
  RE2 block_re("(.*?)[(][|](.*?)[|][)]", options);
  duckdb_re2::StringPiece input(trimmed_string);
  std::string pre_block_command;
  std::string block_command;

  bool failed = false;
  bool block_found = false;
  std::stringstream ss;

  while (RE2::Consume(&input, block_re, &pre_block_command, &block_command)) {
    block_found = true;
    ss << pre_block_command;
    ss << "(";
    transform_block(block_command, ss, failed);
    ss << ")";
  }
  std::string post_block_command;
  post_block_command = input.ToString();
  if (block_found) {
    ss << post_block_command;
  } else {
    transform_block(post_block_command, ss, failed);
  }
  auto sql_query_or_error = ss.str();

  if (failed) {
    // sql_query_or_error contains error string
    // TODO: decide when to consider it a PRQL failure vs this parser extension
    //  not being responsible for parsing the statement.
    //  For now, we consider any malformed statement a PRQL one.
    //  In the future we can try using a couple of basic heuristics to classify
    //  (e.g. only those queries starting with "from"),
    //  or we could mandate that PRQL queries in duckdb are always prefixed with
    //  a certain string / symbol, e.g. |>, and then remove that here.
    return ParserExtensionParseResult(std::move(sql_query_or_error));
  }

  // printf("%s\n", sql_query_or_error.c_str());

  Parser parser; // TODO Pass (ClientContext.GetParserOptions());
  parser.ParseQuery(sql_query_or_error);
  auto statements = std::move(parser.statements);

  return ParserExtensionParseResult(
      make_uniq_base<ParserExtensionParseData, PrqlParseData>(
          std::move(statements[0])));
}

ParserExtensionPlanResult
prql_plan(ParserExtensionInfo *, ClientContext &context,
          unique_ptr<ParserExtensionParseData> parse_data) {
  // We stash away the ParserExtensionParseData before throwing an exception
  // here. This allows the planning to be picked up by prql_bind instead, but
  // we're not losing important context.
  auto prql_state = make_shared<PrqlState>(std::move(parse_data));
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
        auto prql_binder = Binder::CreateBinder(context, &binder);
        auto prql_parse_data =
            dynamic_cast<PrqlParseData *>(prql_state->parse_data.get());
        auto statement = prql_binder->Bind(*(prql_parse_data->statement));
        return statement;
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
