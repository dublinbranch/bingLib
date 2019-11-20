#include "bing.h"
#include "QStacker/qstacker.h"
#include "define/define.h"
#include "fileFunction/filefunction.h"
#include "minMysql/min_mysql.h"
#include "rapidjson/includeMe.h"
#include "xPath/xml.h"
#include <QDateTime>
#include <QDebug>
#include <QMap>
#include <minCurl/curlpp.h>
#include <minCurl/mincurl.h>
#include <unistd.h>

//QByteArray fetchRemoteCampaignId(quint64 campaign_id, int dsp_id) {
//	static QString                   query = "select remote_id from DSPRemote.campaign_has_remote where campaign_id = %1 and dsp_id = %2";
//	static QMap<QString, QByteArray> cache;
//	static std::mutex                mutex;
//	static const QByteArray          def = "0";
//	QByteArray                       val;

//	QString sql = query.arg(campaign_id).arg(dsp_id);
//	mutex.lock();
//	auto iter = cache.constFind(sql);
//	if (iter != cache.cend()) {
//		val = iter.value();
//		mutex.unlock();
//		return val;
//	}
//	mutex.unlock();

//	auto lst = db.query(sql);
//	if (lst.size() > 0) {
//		val = lst.at(0).value("remote_id");
//		mutex.lock();
//		cache.insert(sql, val);
//		mutex.unlock();
//	} else {
//		val = def;
//	}
//	return val;
//}

QMap<QString, QString> BingLib::nationCodes   = BingLib::initNationCodes();
QMap<QString, QString> BingLib::languageCodes = BingLib::initLanguageCodes();

BingLib::BingLib(BingSpecialization* spec) {
	auto builder = CURLpp::Builder()
	                   .set_timeout(10000)
	                   .set_connect_timeout(2000);
	curlpp     = new CURLpp(builder);
	this->spec = spec;
}

/**
 * @brief Bing::insertAds will use a continuously changing sintax and layout, we just pass the payload
 * in useless and impossible to create a do it all formatter
 * @param payload
 * @return
 */
bool BingLib::insertAds(const QByteArray& payload) {
	std::string url;
	if (spec->sandBox) {
		url = "https://campaign.api.sandbox.bingads.microsoft.com/Api/Advertiser/CampaignManagement/V13/CampaignManagementService.svc?wsdl";
	} else {
		url = "https://campaign.api.bingads.microsoft.com/Api/Advertiser/CampaignManagement/V13/CampaignManagementService.svc?wsdl";
	}

	auto marx = curlpp->getMarx();
	curl_easy_setopt(marx, CURLOPT_POST, 1);
	curl_easy_setopt(marx, CURLOPT_POSTFIELDS, payload.constData());
	curl_easy_setopt(marx, CURLOPT_VERBOSE, 1);

	curlpp->resetHeader();
	curlpp->setUrl(url);
	curlpp->addHeader("Content-Type: text/xml;charset=UTF-8");
	curlpp->addHeader("SOAPAction: AddAds");

	auto response = QString::fromStdString(curlpp->perform());

	if (!errorCheck(response)) {
		return false;
	}
	//we do not really care about the mapping ATM
	return true;
}

/**
 * @brief Bing::createParamFile	replace the keys toReplace inside skeletonUrl with their values and writes the result in a file
 * @param skeletonUrl	the file we want to use as skeleton
 * @param toReplace		pairs of pattern->value to replace. the patterns are usually inside double curly brackets
 * @return				the name of the file with all the pattern replaced if writeToFile is true, the content of the file otherwise
 */
QString BingLib::createParamFile(QString skeletonUrl, QMap<QString, QByteArray> toReplace) {
	QString fileContent = fileGetContents(skeletonUrl, false);
	if (fileContent.size() < 10) {
		throw QSL("the template file is impossible small!") + skeletonUrl + QStacker();
	}
	for (auto e : toReplace.toStdMap()) { //orrible but nice
		fileContent.replace(e.first, e.second);
	}
	//fileContent.replace('\n', "");
	return fileContent;
}

QString BingLib::createParamFile(QString skeletonUrl, QMap<QString, QString> toReplace) {
	QString fileContent = fileGetContents(skeletonUrl, false);
	for (auto e : toReplace.toStdMap()) { //orrible but nice
		fileContent.replace(e.first, e.second);
	}
	//fileContent.replace('\n', "");
	return fileContent;
}

QByteArray BingLib::getCampaignsInfo() {
	std::string url;
	if (spec->sandBox) {
		url = "https://campaign.api.sandbox.bingads.microsoft.com/Api/Advertiser/CampaignManagement/V13/CampaignManagementService.svc?wsdl";
	} else {
		url = "https://campaign.api.bingads.microsoft.com/Api/Advertiser/CampaignManagement/V13/CampaignManagementService.svc?wsdl";
	}

	QMap<QString, QByteArray> to_substitute{
	    {"{{header_auth}}", getHeader()},
	    {"{{account_id}}", spec->accountId}};
	QString params = createParamFile("BingReplicationAPI/samples_working/GetCampaigns_tmpl.xml", to_substitute);

	CURLpp curlHandler = CURLpp::Builder()
	                         .set_url(url)
	                         .set_timeout(10000)
	                         .set_connect_timeout(2000)
	                         .add_http_header("Content-Type: text/xml;charset=UTF-8")
	                         .add_http_header("SOAPAction: GetCampaignsByAccountId")
	                         .set_post_fields(params.toStdString())
	                         .set_verbose(0)
	                         .build();

	QByteArray response = curlHandler.perform().c_str();
	QString lastError = QString::fromStdString(curlHandler.getLastError());

	if (!errorCheck(response)) {
		qCritical() << lastError;
		return QByteArray();
	}

	return response;
}

QByteArray BingLib::getAccessToken() {
	std::string url;
	if (spec->sandBox) {
		url = "https://login.live-int.com/oauth20_token.srf";
	} else {
		url = "https://login.live.com/oauth20_token.srf";
	}

	if (token_expires > QDateTime::currentSecsSinceEpoch()) {
		return token;
	}

	QMap<QString, QByteArray> to_substitute{
	    {"{{client_id}}", spec->applicationToken},
	    {"{{refresh_token}}", spec->refreshToken}};

	QString params = createParamFile("BingReplicationAPI/refreshtoken.post", to_substitute);

	CURLpp curlHandler = CURLpp::Builder()
	                         .set_url(url)
	                         .set_timeout(10000)
	                         .set_connect_timeout(2000)
	                         .add_http_header("application/x-www-form-urlencoded")
	                         .set_post_fields(params.toStdString())
	                         .build();

	auto response = curlHandler.perform();
	try {
		rapidjson::Document json;
		json.Parse(response.c_str(), response.size());

		token         = json["access_token"].GetString();
		token_expires = QDateTime::currentSecsSinceEpoch() + 3540; // I'll leave a minute of margin
	} catch (...) {
		qCritical() << "error decoding token json" << response.c_str();
	}

	return token;
}

QByteArray BingLib::getHeader(int type) {
	QByteArray header;
	switch (type) {
	case 0:
		static QByteArray h0 = R"EOD(
<ApplicationToken i:nil="false">{{ApplicationToken}}</ApplicationToken>
<AuthenticationToken  i:nil="false">{{AuthenticationToken}}</AuthenticationToken >
<CustomerAccountId i:nil="false">{{CustomerAccountId}}</CustomerAccountId>
<CustomerId i:nil="false">{{CustomerId}}</CustomerId>
<DeveloperToken i:nil="false">{{DeveloperToken}}</DeveloperToken>
)EOD";
		header               = h0;
		break;
	case 1:
		static const QByteArray skel = R"EOD(
<ns1:CustomerAccountId>{{CustomerAccountId}}</ns1:CustomerAccountId>
<ns1:CustomerId>{{CustomerId}}</ns1:CustomerId>
<ns1:DeveloperToken>{{DeveloperToken}}</ns1:DeveloperToken>
<ns1:AuthenticationToken>{{AuthenticationToken}}</ns1:AuthenticationToken>
)EOD";
		header                       = skel;
		break;
	}

	header.replace("{{CustomerAccountId}}", spec->accountId);
	header.replace("{{CustomerId}}", spec->customerId);
	header.replace("{{DeveloperToken}}", spec->developerToken);
	header.replace("{{ApplicationToken}}", spec->applicationToken);
	header.replace("{{AuthenticationToken}}", getAccessToken());

	return header;
}

std::string BingLib::postFieldsForInsertGroup(const sqlRow& data) {
	//with current logic a banner can have ONLY one group bound, so if we have duplication with that, is a BAD error...
	//quint64 status = data.value("statusng").toULong();
	bool enabled = true; //(status & 1) == 1 && (status & CAMPAIGN_STATUSNG_PAUSED_ALL) == 0;

	QMap<QString, QByteArray> to_substitute({{"{{header_auth}}", getHeader()},
	                                         {"{{campaign_id}}", data.value("campaignRemoteId")},
	                                         {"{{ad_rotation_type}}", "OptimizeForClicks"},
	                                         {"{{ad_group_name}}", data.value("name")},
	                                         {"{{ad_group_status}}", enabled ? "Active" : "Paused"}});

	return createParamFile("BingReplicationAPI/samples_working/AddAdGroups_tmpl.xml", to_substitute).toStdString();
}

std::string BingLib::postFieldsForInsertKeyword(const sqlRow& data) {
	QString keyword_matchtype = data.value("MatchType"); //Qbytearrayref is missing some function -.-
	keyword_matchtype[0]      = keyword_matchtype[0].toUpper();

	QMap<QString, QByteArray> to_substitute({{"{{header_auth}}", getHeader()},
	                                         {"{{ad_group_id}}", data.value("adGroupRemoteId")},
	                                         {"{{bid_amount}}", data.value("bid")},
	                                         {"{{match_type}}", keyword_matchtype.toUtf8()},
	                                         {"{{keyword_status}}", "Active"},
	                                         {"{{keyword_text}}", data.value("keyword")}});

	return createParamFile("BingReplicationAPI/samples_working/AddKeywords_tmpl.xml", to_substitute).toStdString();
}

QString BingLib::insertGroup(const sqlRow& data) {
	std::string url;
	if (spec->sandBox) {
		url = "https://campaign.api.sandbox.bingads.microsoft.com/Api/Advertiser/CampaignManagement/V13/CampaignManagementService.svc?wsdl";
	} else {
		url = "https://campaign.api.bingads.microsoft.com/Api/Advertiser/CampaignManagement/V13/CampaignManagementService.svc?wsdl";
	}
	bool    ok;
	quint64 remote_campaign_id = data.value("campaignRemoteId").toULongLong(&ok);
	if (!ok || remote_campaign_id == 0) {
		throw QString("remote campaign id invalido");
	}

	curlpp->resetHeader();

	//quint64 remote_group_id = fetchRemoteGroupId(remote_campaign_id, getCode()).toULongLong();
	//auto remote_banner_info = fetchRemoteBannerInfo(banner_id, getCode());
	//quint64 remote_group_id = remote_banner_info.remoteGroupId;
	//quint64 remote_keyword_id = remote_banner_info.remoteKeywordId;

	std::string params = postFieldsForInsertGroup(data);

	auto marx = curlpp->getMarx();

	curl_easy_setopt(marx, CURLOPT_POST, 1);
	curl_easy_setopt(marx, CURLOPT_POSTFIELDS, params.c_str());
	curl_easy_setopt(marx, CURLOPT_VERBOSE, false);

	curlpp->addHeader("Content-Type: text/xml;charset=UTF-8");
	curlpp->addHeader("SOAPAction: AddAdGroups");
	curlpp->setPost(params);
	curlpp->setUrl(url);

	auto response = QString::fromStdString(curlpp->perform());

	if (!errorCheck(response)) {
		qCritical() << " \n\n\n\n Processing" << data.value("name") << "\n---------------------------------------------------------\n---------------------------------------------------------\n---------------------------------------------------------";
		return response;
	}

	//TODO
	//qCritical() << "extract the adGroupId";
	//remote_group_id = stoull(json["s:Body"]["AddAdGroupsResponse"]["AdGroupIds"]["a:long"].asString());
	//return_value.group_id = remote_group_id;

	//no errors so far.. trying to insert the keyword
	/*
	quint64 keyword_id = insertKeyword(remote_group_id, data);
	if (keyword_id == 0) {
		qCritical() << "error in creating the keyword for the banner" << banner_id;
		return_value.error = 1;
		return return_value;
	}
	*/

	//return_value.keyword_id = keyword_id;
	return QString();
}

quint64 BingLib::insertSingleKeyword(const sqlRow& data) {
	std::string url;
	if (spec->sandBox) {
		url = "https://campaign.api.sandbox.bingads.microsoft.com/Api/Advertiser/CampaignManagement/V13/CampaignManagementService.svc?wsdl";
	} else {
		url = "https://campaign.api.bingads.microsoft.com/Api/Advertiser/CampaignManagement/V13/CampaignManagementService.svc?wsdl";
	}

	std::string params = postFieldsForInsertKeyword(data);

	curlpp->setUrl(url);
	curlpp->addHeader("Content-Type: text/xml;charset=UTF-8");
	curlpp->addHeader("SOAPAction: AddKeywords");
	curlpp->setPost(params);

	auto response = QString::fromStdString(curlpp->perform());
	if (!errorCheck(response)) {
		return 0;
	}
	XPath xml(response);
	auto  res = xml.getLeaf("//*[name()='AddKeywordsResponse']//*[local-name()='long']");

	auto sql = QSL("UPDATE bing.kw SET remoteId = %1 WHERE id = %2")
	               .arg(QString(res))
	               .arg(QString(data.value("id")));

	db->query(sql);
	return 0;
}

void BingLib::insertMultipleKeyword(const sqlResult& data) {
	std::string url;
	if (spec->sandBox) {
		url = "https://campaign.api.sandbox.bingads.microsoft.com/Api/Advertiser/CampaignManagement/V13/CampaignManagementService.svc?wsdl";
	} else {
		url = "https://campaign.api.bingads.microsoft.com/Api/Advertiser/CampaignManagement/V13/CampaignManagementService.svc?wsdl";
	}

	if (data.isEmpty()) {
		return;
	}
	static const QByteArray keySkel = R"EOD(
<Keyword>
<Bid i:nil="false">
	<Amount i:nil="false">{{bid}}</Amount>
</Bid>
<MatchType i:nil="false">{{match_type}}</MatchType>
<Status i:nil="false">{{keyword_status}}</Status>
<Text i:nil="false"><![CDATA[{{keyword_text}}]]></Text>
</Keyword>
)EOD";

	static const QByteArray EnvSkel = R"EOD(
	<s:Envelope xmlns:i="http://www.w3.org/2001/XMLSchema-instance" xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">
	  <s:Header xmlns="https://bingads.microsoft.com/CampaignManagement/v13">
		<Action mustUnderstand="1">AddKeywords</Action>
		{{header_auth}}
		</s:Header>
	  <s:Body>
		<AddKeywordsRequest xmlns="https://bingads.microsoft.com/CampaignManagement/v13">
		  <AdGroupId>{{ad_group_id}}</AdGroupId>
		  <Keywords i:nil="false">
	{{KEYWORD}}
		  </Keywords>
		</AddKeywordsRequest>
	  </s:Body>
	</s:Envelope>
)EOD";

	QByteArray keys;
	keys.reserve(200000);
	for (auto&& line : data) {
		auto    bid               = QByteArray::number(std::max(0.05, line.value("bid").toDouble()));
		auto    copy              = keySkel;
		QString keyword_matchtype = line.value("MatchType"); //Qbytearrayref is missing some function -.-
		keyword_matchtype[0]      = keyword_matchtype[0].toUpper();
		copy.replace("{{match_type}}", keyword_matchtype.toUtf8());
		copy.replace("{{keyword_status}}", "Active");
		copy.replace("{{keyword_text}}", line.value("keyword"));
		copy.replace("{{bid}}", bid);

		keys.append(copy);
	}
	//any line should be ok... //TODO add some check ?
	auto& line = *data.begin();
	auto  env  = EnvSkel;
	env.replace("{{header_auth}}", getHeader());
	env.replace("{{ad_group_id}}", line.value("adGroupRemoteId"));
	env.replace("{{KEYWORD}}", keys);

	curlpp->resetHeader();
	curlpp->setUrl(url);
	curlpp->addHeader("Content-Type: text/xml;charset=UTF-8");
	curlpp->addHeader("SOAPAction: AddKeywords");
	auto cry = curlpp->getMarx();
	curl_easy_setopt(cry, CURLOPT_POST, 1);
	curl_easy_setopt(cry, CURLOPT_POSTFIELDS, env.constData());
	curl_easy_setopt(cry, CURLOPT_VERBOSE, true);

	auto response = QString::fromStdString(curlpp->perform());
	if (!errorCheck(response)) {
		return;
	}

	XPath xml(response);
	auto  res = xml.getLeafs("//*[name()='AddKeywordsResponse']//*[local-name()='long']");

	//This method is very WEAK, as soon as we have to manage the KW we MUST call the API to fetch remoteId / kw. than using the KW as UNIQ key update our local database
	//one group at a time.

	//This is just to avoid reinserting the same stuff over and over again (which will trigger an error guaranteed)
	SQLBuffering buffer(db);
	for (int i = 0; i < res.size(); i++) {
		QString remoteId = res.at(i);
		QString localId  = data.at(i).value("id");
		auto    sql      = QSL("UPDATE bing.kw SET remoteId = %1 WHERE id = %2;\n")
		               .arg(remoteId)
		               .arg(localId);
		buffer.append(sql);
	}
	return;
}

QString BingLib::convertDeviceMask(quint64 remote_campaign_id, const quint64& deviceMask) {
	static QString singleCriterion = "<CampaignCriterion i:type=\"BiddableCampaignCriterion\" xmlns:i=\"http://www.w3.org/2001/XMLSchema-instance\">"
	                                 "<CampaignId>%1</CampaignId>"
	                                 "<Criterion i:nil=\"false\" i:type=\"DeviceCriterion\">"
	                                 "<DeviceName i:nil=\"false\">%2</DeviceName>"
	                                 "</Criterion>    "
	                                 "<CriterionBid i:nil=\"false\" i:type=\"FixedBid\">"
	                                 "<Type i:nil=\"false\">Fixed</Type>"
	                                 "<Amount>%3</Amount>"
	                                 "</CriterionBid>"
	                                 "</CampaignCriterion>    ";

	int amount_smartphone = 0, amount_tablet = 0, amount_computer = 0;

	if (deviceMask != 0) {
		//filter Devices
		if ((deviceMask & (2)) != 0) { //Desktop, TV, Console
			amount_computer = -100;
		}
		if ((deviceMask & (4 | 64)) != 0) { //Smartphone, Mobile
			amount_smartphone = -100;
		}
		if ((deviceMask & 8) != 0) { //Tablet
			amount_tablet = -100;
		}
		if ((deviceMask & (1)) > 0) { //off = active everything
			amount_computer   = 0;
			amount_tablet     = 0;
			amount_smartphone = 0;
		}
	}

	return singleCriterion.arg(remote_campaign_id)
	           .arg("Computers")
	           .arg(amount_computer) +
	       singleCriterion.arg(remote_campaign_id)
	           .arg("Smartphones")
	           .arg(amount_smartphone) +
	       singleCriterion.arg(remote_campaign_id)
	           .arg("Tablets")
	           .arg(amount_tablet);
}

QString BingLib::getNationCodeList(quint64 remote_campaign_id, const QStringList& nations) {
	static QString singleCriterion = "<CampaignCriterion i:type=\"BiddableCampaignCriterion\" xmlns:i=\"http://www.w3.org/2001/XMLSchema-instance\">"
	                                 "<CampaignId>%1</CampaignId>"
	                                 "<Criterion i:nil=\"false\" i:type=\"LocationCriterion\">"
	                                 "<LocationId i:nil=\"false\">%2</LocationId>"
	                                 "<LocationType i:nil=\"false\">Country</LocationType>"
	                                 "</Criterion>    "
	                                 "<CriterionBid i:nil=\"false\" i:type=\"FixedBid\">"
	                                 "<Type i:nil=\"false\">Fixed</Type>"
	                                 "<Amount>0.00</Amount>"
	                                 "</CriterionBid>"
	                                 "</CampaignCriterion>    ";

	QString criterionList{""};
	for (auto nation : nations) {
		criterionList += singleCriterion
		                     .arg(remote_campaign_id)
		                     .arg(nationCodes.value(nation));
	}

	return criterionList;
}

QString BingLib::getTrackingInfos(const QMap<QByteArray, QByteArray>& data) {
	static const QString trackingUrl =
	    "<TrackingUrlTemplate i:nil=\"false\">%1</TrackingUrlTemplate>  ";
	static const QString customParameters =
	    "<UrlCustomParameters xmlns:e119=\"http://schemas.datacontract.org/2004/07/Microsoft.AdCenter.Advertiser.CampaignManagement.Api.DataContracts.V13\" i:nil=\"false\">"
	    "<e119:Parameters i:nil=\"false\">%1</e119:Parameters>"
	    "</UrlCustomParameters>  ";
	static const QString singleParameter =
	    "<e119:CustomParameter>"
	    "<e119:Key i:nil=\"false\">%1</e119:Key>"
	    "<e119:Value i:nil=\"false\">%2</e119:Value>"
	    "</e119:CustomParameter>   ";

	QString url                  = data.value("code");
	QString trackingBlock        = "";
	QString customParameterBlock = "";

	if (!data.value("K1").isEmpty()) {
		trackingBlock.append(trackingUrl.arg(url.replace('&', "&amp;")));
	}

	if (!data.value("K1").isEmpty()) {
		customParameterBlock.append(singleParameter
		                                .arg(data.value("K1").constData())   // key
		                                .arg(data.value("V1").constData())); // value
	}

	if (!data.value("K2").isEmpty()) {
		customParameterBlock.append(singleParameter
		                                .arg(data.value("K2").constData())   // key
		                                .arg(data.value("V2").constData())); // value
	}

	if (!data.value("K3").isEmpty()) {
		customParameterBlock.append(singleParameter
		                                .arg(data.value("K3").constData())   // key
		                                .arg(data.value("V3").constData())); // value
	}

	if (trackingBlock.isEmpty()) {
		return "";
	}
	if (customParameterBlock.isEmpty()) {
		return trackingBlock;
	}

	return trackingBlock + customParameters.arg(customParameterBlock);
}

/**
 * @brief Bing::errorCheck
 * @param lastError the CURLpp lasterror
 * @param response the raw response from CURLpp
 * @param json the correstponding json of the response
 * @param responseType the json tag that we are supposed to find PartialErrors into (after root->s:Body)
 * @return false if there is an error, true otherwise
 */
bool BingLib::errorCheck(const QString& response) {
	return errorCheck(response.toUtf8());
}

bool BingLib::errorCheck(const QByteArray &response) {
	bool a = response.contains("Authentication failed");
	bool b = response.contains("PartialErrors");
	bool c = response.contains("s:Fault");

	if (a) {
		// will reset the token with the next call
		token_expires = 0;
	}

	if (c) {
		qCritical().noquote() << "last error " << response;
		return false;
	}

	if (b) {
		//Check if there are actual element inside, sometimes is sent an empty block ...
		//TODO this is very expensive, we should return this xml / use somehow!
		XPath xml(response);
		auto  res = xml.getLeafs("//*[name()='PartialErrors']");
		if (!res.isEmpty() && res.at(0).length() > 2) {
			qCritical().noquote() << res.at(0);
			return false;
		}
		return true;
	}
	return true;
}
/*
Json::Value Bing::responseToJSON(std::string resp) {
	QByteArray xmlText = QByteArray::fromStdString(resp);
	//Get your xml into xmlText(you can use QString instead og QByteArray)
	QDomDocument doc;
	doc.setContent(xmlText);
	Json::StyledWriter styledWriter;

	Json::Value translated;
	auto ret = XMLtoJSON(doc.firstChild(), "root", translated);

	std::string tra = styledWriter.write(translated);
	superLog("json_translated", Stream::Generic, 5, ALL) << tra;

	Json::Value jsonResponse;
	Json::Reader reader;

	reader.parse(ret.toStdString().c_str(), jsonResponse);

	std::string fullString = styledWriter.write(jsonResponse);

	return jsonResponse;
}
*/
QMap<QString, QString> BingLib::initNationCodes() {
	static QMap<QString, QString> map{
	    {"AF", "221"}, {"AL", "1"}, {"AQ", "2"}, {"DZ", "3"}, {"AS", "4"}, {"AD", "5"}, {"AO", "6"}, {"AG", "7"}, {"AZ", "203"}, {"AR", "8"}, {"AU", "9"}, {"AT", "10"}, {"BS", "11"}, {"BH", "204"}, {"BD", "12"}, {"AM", "205"}, {"BB", "13"}, {"BE", "14"}, {"BM", "15"}, {"BT", "16"}, {"BO", "17"}, {"BA", "198"}, {"BW", "18"}, {"BR", "20"}, {"BZ", "21"}, {"SB", "23"}, {"VG", "24"}, {"BN", "25"}, {"BG", "26"}, {"MM", "27"}, {"BI", "28"}, {"BY", "29"}, {"KH", "30"}, {"CM", "31"}, {"CA", "32"}, {"CV", "33"}, {"KY", "34"}, {"CF", "35"}, {"LK", "36"}, {"TD", "37"}, {"CL", "38"}, {"CN", "39"}, {"TW", "40"}, {"CX", "41"}, {"CC", "42"}, {"CO", "43"}, {"KM", "226"}, {"YT", "44"}, {"CG", "45"}, {"CD", "46"}, {"CK", "47"}, {"CR", "48"}, {"HR", "49"}, {"CY", "206"}, {"CZ", "51"}, {"BJ", "52"}, {"DK", "53"}, {"DM", "54"}, {"DO", "55"}, {"EC", "56"}, {"SV", "57"}, {"GQ", "58"}, {"ET", "59"}, {"ER", "60"}, {"EE", "61"}, {"FO", "62"}, {"FK", "63"}, {"FJ", "199"}, {"FI", "65"}, {"FR", "66"}, {"GF", "67"}, {"PF", "68"}, {"DJ", "69"}, {"GA", "70"}, {"GE", "208"}, {"GM", "71"}, {"PS", "220"}, {"DE", "72"}, {"GH", "73"}, {"GI", "74"}, {"KI", "75"}, {"GR", "76"}, {"GL", "77"}, {"GD", "78"}, {"GP", "79"}, {"GU", "80"}, {"GT", "81"}, {"GN", "82"}, {"GY", "83"}, {"HT", "84"}, {"VA", "86"}, {"HN", "87"}, {"HK", "200"}, {"HU", "88"}, {"IS", "89"}, {"IN", "90"}, {"ID", "91"}, {"IQ", "228"}, {"IE", "92"}, {"IL", "210"}, {"IT", "93"}, {"CI", "94"}, {"JM", "95"}, {"JP", "96"}, {"KZ", "97"}, {"JO", "211"}, {"KE", "98"}, {"KR", "100"}, {"KW", "212"}, {"KG", "101"}, {"LA", "102"}, {"LB", "213"}, {"LS", "103"}, {"LV", "104"}, {"LR", "105"}, {"LY", "106"}, {"LI", "107"}, {"LT", "108"}, {"LU", "109"}, {"MO", "201"}, {"MG", "110"}, {"MW", "111"}, {"MY", "112"}, {"MV", "113"}, {"ML", "114"}, {"MT", "115"}, {"MQ", "116"}, {"MR", "117"}, {"MU", "118"}, {"MX", "119"}, {"MC", "120"}, {"MN", "121"}, {"MD", "122"}, {"MS", "123"}, {"MA", "124"}, {"MZ", "125"}, {"OM", "214"}, {"NA", "126"}, {"NR", "127"}, {"NP", "128"}, {"NL", "129"}, {"BQ", "130"}, {"AW", "131"}, {"NC", "132"}, {"VU", "133"}, {"NZ", "134"}, {"NI", "135"}, {"NE", "136"}, {"NG", "137"}, {"NU", "223"}, {"NF", "138"}, {"NO", "139"}, {"MP", "140"}, {"FM", "141"}, {"MH", "142"}, {"PW", "143"}, {"PK", "144"}, {"PA", "145"}, {"PG", "146"}, {"PY", "147"}, {"PE", "148"}, {"PH", "149"}, {"PN", "150"}, {"PL", "151"}, {"PT", "152"}, {"GW", "153"}, {"TL", "222"}, {"PR", "154"}, {"QA", "215"}, {"RE", "155"}, {"RO", "224"}, {"RU", "156"}, {"RW", "157"}, {"SH", "229"}, {"KN", "230"}, {"AI", "158"}, {"LC", "231"}, {"PM", "232"}, {"VC", "233"}, {"SM", "159"}, {"ST", "160"}, {"SA", "216"}, {"SN", "161"}, {"SC", "162"}, {"SL", "163"}, {"SG", "164"}, {"SK", "165"}, {"VN", "166"}, {"SI", "167"}, {"SO", "225"}, {"ZA", "168"}, {"ZW", "169"}, {"ES", "170"}, {"SR", "172"}, {"SZ", "173"}, {"SE", "174"}, {"CH", "175"}, {"TJ", "176"}, {"TH", "177"}, {"TG", "178"}, {"TK", "179"}, {"TO", "180"}, {"TT", "181"}, {"AE", "218"}, {"TN", "227"}, {"TR", "182"}, {"TM", "183"}, {"TC", "184"}, {"TV", "234"}, {"UG", "185"}, {"UA", "235"}, {"MK", "186"}, {"EG", "187"}, {"GB", "188"}, {"TZ", "189"}, {"US", "190"}, {"VI", "237"}, {"BF", "191"}, {"UY", "192"}, {"UZ", "193"}, {"VE", "202"}, {"WF", "194"}, {"WS", "195"}, {"YE", "219"}, {"ZM", "196"}};
	return map;
}

QMap<QString, QString> BingLib::initLanguageCodes() {
	static QMap<QString, QString> map{
	    {"ar", "Arabic"},
	    {"da", "Danish"},
	    {"nl", "Dutch"},
	    {"en", "English"},
	    {"fi", "Finnish"},
	    {"fr", "French"},
	    {"de", "German"},
	    {"he", "Hebrew"},
	    {"it", "Italian"},
	    {"ja", "Japanese"},
	    {"ko", "Korean"},
	    {"nn", "Norwegian"},
	    {"pt", "Portuguese"},
	    {"ru", "Russian"},
	    {"zh", "SimplifiedChinese"},
	    {"es", "Spanish"},
	    {"sv", "Swedish"},
	    {"zh", "TraditionalChinese"},

	};
	return map;
}

double return1() {
	return 1;
}

QString BingLib::getGroupInfo(const QByteArray& remote_campaign_id) {
	std::string url;
	if (spec->sandBox) {
		url = "https://campaign.api.sandbox.bingads.microsoft.com/Api/Advertiser/CampaignManagement/V13/CampaignManagementService.svc?wsdl";
	} else {
		url = "https://campaign.api.bingads.microsoft.com/Api/Advertiser/CampaignManagement/V13/CampaignManagementService.svc?wsdl";
	}

	//	QString params = getFile("BingReplicationAPI/samples_working/GetAdGroupsByCampaignId_tmpl.xml");
	QMap<QString, QByteArray> to_substitute{
	    {"{{header_auth}}", getHeader()},
	    {"{{campaign_id}}", remote_campaign_id}};
	QString params = createParamFile("BingReplicationAPI/samples_working/GetAdGroupsByCampaignId_tmpl.xml", to_substitute);

	CURLpp curlHandler = CURLpp::Builder()
	                         .set_url(url)
	                         .set_timeout(10000)
	                         .set_connect_timeout(2000)
	                         .add_http_header("Content-Type: text/xml;charset=UTF-8")
	                         .add_http_header("SOAPAction: GetAdGroupsByCampaignId")
	                         .set_post_fields(params.toStdString())
	                         .build();

	auto response = QString::fromStdString(curlHandler.perform());
	if (!errorCheck(response)) {
		return QString();
	}
	return response;
}

QByteArray BingLib::getAdGroupExpenditure(const QDateTime& day) {
	QByteArray skel = R"EOD(
<SOAP-ENV:Envelope xmlns:SOAP-ENV="http://schemas.xmlsoap.org/soap/envelope/" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmlns:ns1="https://bingads.microsoft.com/Reporting/v13" xmlns:ns2="http://schemas.microsoft.com/2003/10/Serialization/Arrays">
  <SOAP-ENV:Header>
	{{header_auth}}
  </SOAP-ENV:Header>
  <SOAP-ENV:Body>
	  <ns1:SubmitGenerateReportRequest>
		  <ns1:ReportRequest xsi:type="ns1:KeywordPerformanceReportRequest">
			  <ns1:ExcludeColumnHeaders xsi:nil="true" />
			  <ns1:ExcludeReportFooter xsi:nil="true" />
			  <ns1:ExcludeReportHeader xsi:nil="true" />
			  <ns1:Format>Xml</ns1:Format>
			  <ns1:ReportName>{{name}}</ns1:ReportName>
			  <ns1:ReturnOnlyCompleteData>false</ns1:ReturnOnlyCompleteData>
			  <ns1:Aggregation>Daily</ns1:Aggregation>
			  <ns1:Columns>
				  <ns1:KeywordPerformanceReportColumn>TimePeriod</ns1:KeywordPerformanceReportColumn>
				  <ns1:KeywordPerformanceReportColumn>AccountId</ns1:KeywordPerformanceReportColumn>
				  <ns1:KeywordPerformanceReportColumn>CampaignId</ns1:KeywordPerformanceReportColumn>
				  <ns1:KeywordPerformanceReportColumn>Keyword</ns1:KeywordPerformanceReportColumn>
				  <ns1:KeywordPerformanceReportColumn>KeywordId</ns1:KeywordPerformanceReportColumn>
				  <ns1:KeywordPerformanceReportColumn>DeviceType</ns1:KeywordPerformanceReportColumn>
				  <ns1:KeywordPerformanceReportColumn>BidMatchType</ns1:KeywordPerformanceReportColumn>
				  <ns1:KeywordPerformanceReportColumn>Clicks</ns1:KeywordPerformanceReportColumn>
				  <ns1:KeywordPerformanceReportColumn>Impressions</ns1:KeywordPerformanceReportColumn>
				  <ns1:KeywordPerformanceReportColumn>Ctr</ns1:KeywordPerformanceReportColumn>
				  <ns1:KeywordPerformanceReportColumn>Spend</ns1:KeywordPerformanceReportColumn>
				  <ns1:KeywordPerformanceReportColumn>QualityScore</ns1:KeywordPerformanceReportColumn>
				  <ns1:KeywordPerformanceReportColumn>AdGroupId</ns1:KeywordPerformanceReportColumn>
				  <ns1:KeywordPerformanceReportColumn>AdGroupStatus</ns1:KeywordPerformanceReportColumn>
				  <ns1:KeywordPerformanceReportColumn>KeywordStatus</ns1:KeywordPerformanceReportColumn>
				  <ns1:KeywordPerformanceReportColumn>AdRelevance</ns1:KeywordPerformanceReportColumn>
				  <ns1:KeywordPerformanceReportColumn>AdDistribution</ns1:KeywordPerformanceReportColumn>
				  <ns1:KeywordPerformanceReportColumn>AllConversions</ns1:KeywordPerformanceReportColumn>
				  <ns1:KeywordPerformanceReportColumn>AveragePosition</ns1:KeywordPerformanceReportColumn>
				  <ns1:KeywordPerformanceReportColumn>Conversions</ns1:KeywordPerformanceReportColumn>
				  <ns1:KeywordPerformanceReportColumn>LandingPageExperience</ns1:KeywordPerformanceReportColumn>
				  <ns1:KeywordPerformanceReportColumn>Network</ns1:KeywordPerformanceReportColumn>
				  <ns1:KeywordPerformanceReportColumn>AdGroupName</ns1:KeywordPerformanceReportColumn>
				  <ns1:KeywordPerformanceReportColumn>CurrentMaxCpc</ns1:KeywordPerformanceReportColumn>
			  </ns1:Columns>
			  <ns1:Filter xsi:nil="true" />
			  <ns1:Scope>
				  <ns1:AccountIds>
					  <ns2:long>{{account_id}}</ns2:long>
				  </ns1:AccountIds>
				  <ns1:AdGroups xsi:nil="true" />
				  <ns1:Campaigns xsi:nil="true" />
			  </ns1:Scope>
			  <ns1:Time>
				  <ns1:CustomDateRangeEnd>
					<ns1:Day>{{day}}</ns1:Day>
					<ns1:Month>{{month}}</ns1:Month>
					<ns1:Year>{{year}}</ns1:Year>
				  </ns1:CustomDateRangeEnd>
				  <ns1:CustomDateRangeStart>
					  <ns1:Day>{{day}}</ns1:Day>
					  <ns1:Month>{{month}}</ns1:Month>
					  <ns1:Year>{{year}}</ns1:Year>
				  </ns1:CustomDateRangeStart>
				  <ns1:ReportTimeZone xsi:nil="true" />
			  </ns1:Time>
		  </ns1:ReportRequest>
	  </ns1:SubmitGenerateReportRequest>
  </SOAP-ENV:Body>
</SOAP-ENV:Envelope>
)EOD";

	skel.replace("{{header_auth}}", getHeader(1));
	skel.replace("{{account_id}}", spec->accountId);
	skel.replace("{{day}}", QByteArray::number(day.date().day()));
	skel.replace("{{month}}", QByteArray::number(day.date().month()));
	skel.replace("{{year}}", QByteArray::number(day.date().year()));
	skel.replace("{{name}}", QSL("Report %1").arg(day.toString(mysqlDateFormat)).toUtf8());

	CURLpp curlHandler = CURLpp::Builder()
	                         .set_url("https://reporting.api.bingads.microsoft.com/Api/Advertiser/Reporting/V13/ReportingService.svc")
	                         .set_timeout(10000)
	                         .set_connect_timeout(2000)
	                         .add_http_header("Content-Type: text/xml;charset=UTF-8")
	                         .add_http_header("SOAPAction: SubmitGenerateReport")
	                         .set_post_fields(skel.toStdString())
	                         .set_verbose(false)
	                         .build();

	auto response = QString::fromStdString(curlHandler.perform());

	XPath xml(response);
	auto  res = xml.getLeaf("//*[name()='ReportRequestId']");
	if (res.isEmpty()) {
		qCritical().noquote() << "errore nel richiedere report" << QStacker();
		return QByteArray();
	}
	return bulkDownloader(res);
}

QByteArray BingLib::bulkDownloader(const QByteArray& remoteId) {
	QByteArray skel = R"EOD(
<SOAP-ENV:Envelope xmlns:SOAP-ENV="http://schemas.xmlsoap.org/soap/envelope/" xmlns:ns1="https://bingads.microsoft.com/Reporting/v13">
  <SOAP-ENV:Header>
	  {{header_auth}}
  </SOAP-ENV:Header>
  <SOAP-ENV:Body>
	  <ns1:PollGenerateReportRequest>
		  <ns1:ReportRequestId>{{remoteId}}</ns1:ReportRequestId>
	  </ns1:PollGenerateReportRequest>
  </SOAP-ENV:Body>
</SOAP-ENV:Envelope>
)EOD";
	skel.replace("{{header_auth}}", getHeader(1));
	skel.replace("{{remoteId}}", remoteId);

	QByteArray downloadUrl;
	//avoid remaining struct
	for (uint i = 0; i < 100; i++) {
		CURLpp curlHandler = CURLpp::Builder()
		                         .set_url("https://reporting.api.bingads.microsoft.com/Api/Advertiser/Reporting/V13/ReportingService.svc")
		                         .set_timeout(10000)
		                         .set_connect_timeout(2000)
		                         .add_http_header("Content-Type: text/xml;charset=UTF-8")
		                         .add_http_header("SOAPAction: PollGenerateReport")
		                         .set_post_fields(skel.toStdString())
		                         .build();
		QByteArray response = curlHandler.perform().c_str();

		//TODO loop until you can find the ReportDownloadUrl

		XPath xml(response);
		downloadUrl = xml.getLeaf("//*[name()='ReportDownloadUrl']");
		if (!downloadUrl.isEmpty()) {
			break;
		}
		usleep(1000000); //1 second
	}

	downloadUrl.replace("&amp;", "&");

	auto content = urlGetContent(downloadUrl);

	cleanFolder("bingReport");

	filePutContents(content, "bingReport/downloadedFile.zip");

	auto files = unzippaFile("bingReport");
	if(files.isEmpty()){
		throw QSL("bing report is empty!");
	}
	return fileGetContents(files.at(0));
}

/*
QString Bing::getAdInfo(quint64 remote_campaign_id, quint64 remote_group_id) {
	if(remote_group_id == 0) {
		return "";
	}
	std::string url = endpoint;

	//QString params = getFile("BingReplicationAPI/samples_working/GetAdsByAdGroupId_tmpl.xml");
	QMap<QString, QByteArray> to_substitute{
		{"{{header_auth}}", getHeader()},
		{"{{ad_group_id}}", QByteArray::number(remote_group_id)}
	};
	QString params = createParamFile("BingReplicationAPI/samples_working/GetAdsByAdGroupId_tmpl.xml", to_substitute, false);

	CURLpp curlHandler = CURLpp::Builder()
						 .set_url(url)
						 .set_timeout(10000)
						 .set_connect_timeout(2000)
						 .add_http_header("Content-Type: text/xml;charset=UTF-8")
						 .add_http_header("SOAPAction: GetAdsByAdGroupId")
						 .set_post_fields(params.toStdString())
						 .build();

	auto response = curlHandler.perform();
	auto json = responseToJSON(response);

	QString lastError = QString::fromStdString(curlHandler.getLastError());
	if(!errorCheck(response, json, "GetAdsByAdGroupIdResponse")) {
		superLog("lastError", Stream::Generic, 0, ALL) << lastError;
		//      analyzeCurlError(lastError);
		return lastError;
	}

	QString res="";
	auto singleCampInfos = [](const Json::Value &val) -> QString{
		QString res = "";
		res.append(QString("<a href = '?maru=youShallN0tP@ss&campaign_id=%1'><h3>banner title: %2</h3></a>").arg(QString::fromStdString(val["Id"].asString())).arg(val["TitlePart1"].asString().c_str()));
		res.append("id: " + QString::fromStdString(val["Id"].asString()));
		res.append("<br>title part 2: " + QString::fromStdString(val["TitlePart2"].asString()));
		res.append("<br>status: " + QString::fromStdString(val["Status"].asString()));
		res.append("<br>path 1: " + QString::fromStdString(val["Path1"].asString()));
		res.append("<br>path 2: " + QString::fromStdString(val["Path2"].asString()));
		res.append("<br>text: " + QString::fromStdString(val["Text"].asString()));
		return res;
	};

	auto data = json["s:Body"]["GetAdsByAdGroupIdResponse"]["Ads"];
	for(Json::ValueIterator singleCampaign = data.begin(); singleCampaign != data.end(); ++singleCampaign) {
		res.append(singleCampInfos(*singleCampaign));
	}

	return res;

}
*/

/*
bool Bing::updateCampaign(quint64 campaign_id, const QMap<QByteArray, QByteArray> &data) {
	std::string url = endpoint;

	auto remote_campaign_id = fetchRemoteCampaignId(campaign_id, getCode());
	std::string params = postFieldsForUpdateCampaign(remote_campaign_id, data);

	CURLpp curlHandler = CURLpp::Builder()
						 .set_url(url)
						 .set_timeout(10000)
						 .set_connect_timeout(2000)
						 .add_http_header("Content-Type: text/xml;charset=UTF-8")
						 .add_http_header("SOAPAction: UpdateCampaigns")
						 .set_post_fields(params)
						 .build();

	auto response = curlHandler.perform();
	extractCurlInfos(curlHandler, 0, campaign_id);

	auto json = responseToJSON(response);

	QString lastError = QString::fromStdString(curlHandler.getLastError());
	if(!errorCheck(response, json, "UpdateCampaignsResponse")) {
		superLog("lastError", Stream::Generic, 0, ALL) << response;
		//      analyzeCurlError(lastError);
		return false;
	}

	updateCampaignLocation(remote_campaign_id.toULongLong(), data);
	updateCampaignDevice(remote_campaign_id.toULongLong(), data);
	return true;
}
*/

/*
std::string Bing::postFieldsForRemoveKeyword(const RemoteBanner &infos) {
	QMap<QString, QByteArray> to_substitute ({
		{"{{header_auth}}", getHeader()},
		{"{{keyword_id}}", infos.getRemoteKeywordQBA()},
		{"{{ad_group_id}}", infos.getRemoteGroupQBA()}
	});

	return createParamFile("BingReplicationAPI/samples_working/DeleteKeywords_tmpl.xml", to_substitute, false).toStdString();
}
*/
/*
std::string Bing::postFieldsForRemoveTargeting(quint64 remote_campaign_id, QString type) {
	QString singleRow {"<a1:long>%1</a1:long>    "};
	QString ids {"<CampaignCriterionIds i:nil=\"false\" xmlns:a1=\"http://schemas.microsoft.com/2003/10/Serialization/Arrays\"> %1 </CampaignCriterionIds>"};
	bool failed;
	QString query = "select * from DSPRemote.targeting_has_campaign where remote_campaign_id = %1 and dsp_id = %2 and type = '%3'";
	auto res = Options::executeQuery(nullptr,query
									 .arg(remote_campaign_id)
									 .arg(getCode())
									 .arg(type)
									 ,failed);

	if(res.size() == 0) {
		superLog("no targets to delete", Stream::Generic, 5, ALL) << remote_campaign_id;
		return "";
	}

	QStringList id_list;
	for(auto sing_row: res) {
		id_list << singleRow.arg(sing_row.value("targeting_id").constData());
	}

	QMap<QString, QByteArray> to_substitute ({
		{"{{header_auth}}", getHeader()},
		{"{{campaign_id}}", QByteArray::number(remote_campaign_id)},
		{"{{criterion_ids}}", ids.arg(id_list.join(" ")).toUtf8()}
	});

	return createParamFile("BingReplicationAPI/samples_working/DeleteCampaignCriterions_tmpl.xml", to_substitute, false).toStdString();
}
*/
/*
QString Bing::XMLtoJSON(QDomNode root, std::string parent_tag, Json::Value & json_output) {
	QString skeleton = "\"%1\"";
	if(root.isNull()) {
		//should never happen
		superLog("null json node", Stream::Generic, 0, ALL);
		return "";
	} else {
		if(root.hasChildNodes()) {
			auto nodes = root.childNodes();
			auto node = nodes.at(0);	//first child
			if(node.isText()) {
				//leaf non empty node
				QString ret = skeleton.arg(root.toElement().text());
				json_output[parent_tag] = root.toElement().text().toStdString();
				return ret;
			}
			//non leaf node
			QStringList siblings;
			int iter = 0;
			while(!node.isNull()) {
				QString tagName = node.toElement().tagName();
				auto this_value = json_output[parent_tag];
				Json::Value::Members gag = this_value.getMemberNames();
				for(auto memb : gag) {
					if(memb.compare(tagName.toStdString()) == 0) {
						//already a member with this name
						tagName += "_" + QString::number(++iter);
					}
				}
				siblings << skeleton.arg(tagName) + ":" +  XMLtoJSON(node, tagName.toStdString(),  json_output[parent_tag]);
				node = node.nextSibling();
			}
			return "{" + siblings.join(",") + "}";
		} else {
			//leaf empty node
			QString ret = skeleton.arg("");
			json_output[parent_tag] = "";
			return ret;
		}
	}
}
*/
/*
bool Bing::updateKeyword(quint64 banner_id, const QMap<QByteArray, QByteArray> &data) {
	auto banner_info = fetchRemoteBannerInfo(banner_id, getCode());

	std::string url = endpoint;
	std::string params = postFieldsForRemoveKeyword(banner_info);

	CURLpp curlHandler = CURLpp::Builder()
						 .set_url(url)
						 .set_timeout(10000)
						 .set_connect_timeout(2000)
						 .add_http_header("Content-Type: text/xml;charset=UTF-8")
						 .add_http_header("SOAPAction: DeleteKeywords")
						 .set_post_fields(params)
						 .build();

	auto response = curlHandler.perform();
	extractCurlInfos(curlHandler, banner_id, data.value("campaign_id").toULongLong());

	auto json = responseToJSON(response);

	QString lastError = QString::fromStdString(curlHandler.getLastError());
	if(!errorCheck(response, json, "DeleteKeywordsResponse")) {
		superLog("lastError", Stream::Generic, 0, ALL) << response;
		//      analyzeCurlError(lastError);
		return false;
	}

	//keyword deleted correctly, now insert the new one!
	quint64 keyword_id = insertKeyword(banner_info.remoteGroupId, data);
	if (keyword_id == 0) {
		superLog("error in creating the keyword for the banner", Stream::Generic, 0, ALL) << banner_id;
		return false;
	}
	updateBannerHasRemote(banner_id, keyword_id);

	return true;
}
*/
/*
bool Bing::updateBannerForReal(quint64 banner_id, const QMap<QByteArray, QByteArray> &data) {

	auto banner_info = fetchRemoteBannerInfo(banner_id, getCode());

	std::string url = endpoint;
	std::string params = postFieldsForUpdateBanner(banner_info, data);

	CURLpp curlHandler = CURLpp::Builder()
						 .set_url(url)
						 .set_timeout(10000)
						 .set_connect_timeout(2000)
						 .add_http_header("Content-Type: text/xml;charset=UTF-8")
						 .add_http_header("SOAPAction: UpdateAds")
						 .set_post_fields(params)
						 .build();

	auto response = curlHandler.perform();
	extractCurlInfos(curlHandler, banner_id, data.value("campaign_id").toULongLong());

	auto json = responseToJSON(response);

	QString lastError = QString::fromStdString(curlHandler.getLastError());
	if(!errorCheck(response, json, "UpdateAdsResponse")) {
		superLog("lastError", Stream::Generic, 0, ALL) << response;
		//      analyzeCurlError(lastError);
		return false;
	}
	return true;
}
*/
/*
bool Bing::removeTargeting(quint64 remote_campaign_id, QString type) {
	superLog("removeTargeting", Stream::Generic, 5, ALL) << remote_campaign_id;
	std::string params = postFieldsForRemoveTargeting(remote_campaign_id, type);

	if(params.empty()) {
		return true;
	}

	std::string url = endpoint;

	CURLpp curlHandler = CURLpp::Builder()
						 .set_url(url)
						 .set_timeout(10000)
						 .set_connect_timeout(2000)
						 .add_http_header("Content-Type: text/xml;charset=UTF-8")
						 .add_http_header("SOAPAction: DeleteCampaignCriterions")
						 .set_post_fields(params)
						 .build();

	auto response = curlHandler.perform();

	auto json = responseToJSON(response);

	QString lastError = QString::fromStdString(curlHandler.getLastError());
	if(!errorCheck(response, json, "DeleteCampaignCriterionsResponse")) {
		superLog("lastError", Stream::Generic, 0, ALL) << response;
		//      analyzeCurlError(lastError);
		return false;
	}

	bool failed;
	QString query = "delete from DSPRemote.targeting_has_campaign where remote_campaign_id = %1 and type = '%3' and dsp_id = %2";
	auto res = Options::executeQuery(nullptr,query
									 .arg(remote_campaign_id)
									 .arg(getCode())
									 .arg(type)
									 ,failed);

	return true;
}
*/

/*
bool Bing::updateBanner(quint64 banner_id, const QMap<QByteArray, QByteArray> &data) {
	bool success = true;

	if(
		data.contains("status") or
		data.contains("c_tare")) {
		success = updateGroup(banner_id, data);
	}

	if(success and (
				data.contains("keyword") or
				data.contains("keyword_matchtype"))) {
		success = updateKeyword(banner_id, data);
	}

	if(success and (
				data.contains("url") or
				data.contains("path1") or
				data.contains("path2") or
				data.contains("text") or
				data.contains("title") or
				data.contains("title2"))) {
		success = updateBannerForReal(banner_id, data);
	}

	if(!success) {
		superLog("something went wrong while updating banner", Stream::Generic, 0, ALL) << banner_id;
		return false;
	}

	return success;
}
*/
/*
Json::Value Bing::getStats() {
	Json::Value ret;
	ret["DSP"] = "Bing";

	ret["common"] = commonValues();
	return ret;
}
*/

/*
QString Bing::getLocations() {
	std::string url = endpoint;

	QMap<QString, QByteArray> to_substitute{
		{"{{header_auth}}", getHeader()}
	};
	QString params = createParamFile("BingReplicationAPI/samples_working/GetLocations_tmpl.xml", to_substitute, false);

	CURLpp curlHandler = CURLpp::Builder()
						 .set_url(url)
						 .set_timeout(10000)
						 .set_connect_timeout(2000)
						 .add_http_header("Content-Type: text/xml;charset=UTF-8")
						 .add_http_header("SOAPAction: GetGeoLocationsFileUrl")
						 .set_post_fields(params.toStdString())
						 .build();

	auto response = curlHandler.perform();

	auto json = responseToJSON(response);
	return "";
}
*/

/*
bool Bing::updateCampaignLocation(quint64 remote_campaign_id, const QMap<QByteArray, QByteArray> &data) {
	superLog("updateCampaignLocation", Stream::Generic, 5, ALL) << remote_campaign_id;

	std::string url = endpoint;
	auto params = postFieldsForUpdateCampaignLocation(remote_campaign_id, data);

	if(params.empty()) {
		return true;
	}

	removeTargeting(remote_campaign_id, "nation");
	CURLpp curlHandler = CURLpp::Builder()
						 .set_url(url)
						 .set_timeout(10000)
						 .set_connect_timeout(2000)
						 .add_http_header("Content-Type: text/xml;charset=UTF-8")
						 .add_http_header("SOAPAction: AddCampaignCriterions")
						 .set_post_fields(params)
						 .build();

	auto response = curlHandler.perform();
	extractCurlInfos(curlHandler, 0, data.value("id").toULongLong());

	auto json = responseToJSON(response);

	QString lastError = QString::fromStdString(curlHandler.getLastError());
	if(!errorCheck(response, json, "AddCampaignCriterionsResponse")) {
		superLog("lastError", Stream::Generic, 0, ALL) << response;
		//      analyzeCurlError(lastError);
		return false;
	}

	// store all locations in targeting_has_campaign
	auto remote_ids = json["s:Body"]["AddCampaignCriterionsResponse"]["CampaignCriterionIds"];
	for(Json::ValueIterator singleId = remote_ids.begin(); singleId != remote_ids.end(); ++singleId) {
		insertTargetingHasCampaign( stoull((*singleId).asString()), remote_campaign_id, targetType::nation, getCode());
	}

	return true;
}
*/
/*
bool Bing::updateCampaignDevice(quint64 remote_campaign_id, const QMap<QByteArray, QByteArray> &data) {
	superLog("updateCampaignDevice", Stream::Generic, 5, ALL) << remote_campaign_id;

	std::string url = endpoint;
	auto params = postFieldsForUpdateCampaignDevice(remote_campaign_id, data);

	if(params.empty()) {
		return true;
	}

	removeTargeting(remote_campaign_id, "device");

	CURLpp curlHandler = CURLpp::Builder()
						 .set_url(url)
						 .set_timeout(10000)
						 .set_connect_timeout(2000)
						 .add_http_header("Content-Type: text/xml;charset=UTF-8")
						 .add_http_header("SOAPAction: AddCampaignCriterions")
						 .set_post_fields(params)
						 .build();

	auto response = curlHandler.perform();
	extractCurlInfos(curlHandler, 0, data.value("id").toULongLong());

	auto json = responseToJSON(response);

	QString lastError = QString::fromStdString(curlHandler.getLastError());
	if(!errorCheck(response, json, "AddCampaignCriterionsResponse")) {
		superLog("lastError", Stream::Generic, 5, ALL) << response;
		//      analyzeCurlError(lastError);
		return false;
	}

	// store all locations in targeting_has_campaign
	auto remote_ids = json["s:Body"]["AddCampaignCriterionsResponse"]["CampaignCriterionIds"];
	for(Json::ValueIterator singleId = remote_ids.begin(); singleId != remote_ids.end(); ++singleId) {
		insertTargetingHasCampaign( stoull((*singleId).asString()), remote_campaign_id, targetType::device, getCode());
	}

	return true;
}
*/

/*
bool Bing::updateGroup(quint64 banner_id, const QMap<QByteArray, QByteArray> &data) {
	superLog("updateGroup", Stream::Generic, 5, ALL) << banner_id;

	auto remote_banner_info = fetchRemoteBannerInfo(banner_id, getCode());
	auto remote_campaign_id = fetchRemoteCampaignId(data.value("campaign_id").toULongLong(), getCode()).toULongLong ();
	change.setBanner_id(banner_id);
	QByteArray params = postFieldsForUpdateGroup(remote_banner_info.remoteGroupId, data);
	if(params.isEmpty()) {
		return false;
	}
	if(!groupToUpdate.contains(remote_campaign_id)) {
		groupToUpdate.insert(remote_campaign_id,new QByteArrayList());
	}
	auto iter = groupToUpdate.find(remote_campaign_id);
	iter.value()->append(params);
	//https://developers.google.com/adwords/api/docs/appendix/limits#general
	//you will srly not reach the size limit
	if(iter.value()->size() > 900) {
		flushUpdateGroup();
	}
	return true;


}
*/
/*
bool Bing::flushUpdateGroup() {
	static const QByteArray UpdateAdGroups_Head = R"EOD(<!-- Request -->
	<s:Envelope xmlns:i="http://www.w3.org/2001/XMLSchema-instance" xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">
	 <s:Header xmlns="https://bingads.microsoft.com/CampaignManagement/v13">
	   <Action mustUnderstand="1">UpdateAdGroups</Action>
	   {{header_auth}}
	 </s:Header>
	 <s:Body>
	   <UpdateAdGroupsRequest xmlns="https://bingads.microsoft.com/CampaignManagement/v13">
	     <CampaignId>{{campaign_id}}</CampaignId>
	     <AdGroups i:nil="false">
	)EOD";

	static const QByteArray UpdateAdGroups_Foot = R"EOD(
	    </AdGroups>
	    </UpdateAdGroupsRequest>
	</s:Body>
	</s:Envelope>
	)EOD";

	if(groupToUpdate.size() == 0) {
		return true;
	}
	auto iter = groupToUpdate.begin ();
	auto end = groupToUpdate.end ();
	for (; iter != end;) {
		QByteArray package = UpdateAdGroups_Head;
		package.replace("{{header_auth}}", getHeader());
		package.replace("{{campaign_id}}", QByteArray::number(iter.key ()));
		package.append(iter.value ()->join());
		package.append(UpdateAdGroups_Foot);
		auto copy = package.toStdString();
		iter.value()->clear ();
		delete(iter.value ());
		iter = groupToUpdate.erase (iter);

		CURLpp curlHandler = CURLpp::Builder()
							 .set_url(endpoint)
							 .set_timeout(10000)
							 .set_connect_timeout(2000)
							 .add_http_header("Content-Type: text/xml;charset=UTF-8")
							 .add_http_header("SOAPAction: UpdateAdGroups")
							 .set_post_fields(copy)
							 .set_verbose(0)
							 .build();

		auto response = curlHandler.perform();
		auto json = responseToJSON(response);

		QString lastError = QString::fromStdString(curlHandler.getLastError());
		if(!errorCheck(response, json, "UpdateAdGroupsResponse")) {
			superLog("lastError", Stream::Generic, 0, ALL) << response;
			//      analyzeCurlError(lastError);
			return false;
		}
	}

	return true;
}
*/
/*
std::string Bing::postFieldsForUpdateCampaign(const QByteArray &remote_campaign_id, const QMap<QByteArray, QByteArray> &data) {
	quint64 status = data.value("statusng").toULong();
	bool enabled = (status & 1) == 1 && (status & CAMPAIGN_STATUSNG_PAUSED_ALL) == 0;
	static QString daily_budget_tag = "<DailyBudget i:nil=\"false\">%1</DailyBudget>";
	static QString name	= "<Name  i:nil=\"false\">%1</Name >";


	// common target to substitute
	QMap<QString, QByteArray> to_substitute({
		{"{{header_auth}}", getHeader()},
		{"{{account_id}}", getAccountId()},
		{"{{campaign_id}}", remote_campaign_id},
		{"{{campaign_status}}", enabled ? "Active" : "Paused"}
	});

	// custom target to substitute
	if(data.contains("cap_daily")) {
		double daily_cap = data.value("cap_daily").toDouble() * getCurrencyConversion();
		to_substitute.insert("{{daily_budget}}", daily_budget_tag.arg(daily_cap).toUtf8());
	} else {
		to_substitute.insert("{{daily_budget}}", "");
	}

	if(data.contains("name")) {
		to_substitute.insert("{{campaign_name}}", name.arg(data.value("name").constData()).toUtf8());
	} else {
		to_substitute.insert("{{campaign_name}}", "");
	}

	return createParamFile("BingReplicationAPI/samples_working/UpdateCampaigns_tmpl.xml", to_substitute, false).toStdString();

}
*/
/*
std::string Bing::postFieldsForUpdateBanner(const RemoteBanner& banner_info, const QMap<QByteArray, QByteArray> &data) {
	static QString url_tag		= "<FinalUrls i:nil=\"false\" xmlns:a1=\"http://schemas.microsoft.com/2003/10/Serialization/Arrays\"><a1:string>%1</a1:string></FinalUrls>";
	static QString path1_tag	= "<Path1 i:nil=\"false\">%1</Path1>";
	static QString path2_tag	= "<Path2 i:nil=\"false\">%1</Path2>";
	static QString text_tag		= "<Text i:nil=\"false\">%1</Text>";
	static QString title1_tag	= "<TitlePart1 i:nil=\"false\">%1</TitlePart1>";
	static QString title2_tag	= "<TitlePart2 i:nil=\"false\">%1</TitlePart2>";

	// common target to substitute
	QMap<QString, QByteArray> to_substitute ({
		{"{{header_auth}}", getHeader()},
		{"{{ad_group_id}}", banner_info.getRemoteGroupQBA()},
		{"{{ad_id}}", banner_info.getRemoteBannerIdQBA()},
		{"{{ad_status}}", data.value("status") == "1" ? "Active" : "Paused"}
	});

	// custom target to substitute
	if(data.contains("url")) {
		QByteArray url = data.value("url");
		to_substitute.insert("{{final_url}}", url_tag.arg(url.replace('&', "&amp;").constData()).toUtf8());
	} else {
		to_substitute.insert("{{final_url}}", "");
	}
	if(data.contains("path1")) {
		to_substitute.insert("{{path_one}}", path1_tag.arg(data.value("path1").constData()).toUtf8());
	} else {
		to_substitute.insert("{{path_one}}", "");
	}
	if(data.contains("path2")) {
		to_substitute.insert("{{path_two}}", path2_tag.arg(data.value("path2").constData()).toUtf8());
	} else {
		to_substitute.insert("{{path_two}}", "");
	}
	if(data.contains("text")) {
		to_substitute.insert("{{ad_text}}", text_tag.arg(data.value("text").constData()).toUtf8());
	} else {
		to_substitute.insert("{{ad_text}}", "");
	}
	if(data.contains("title")) {
		to_substitute.insert("{{title_part_one}}", title1_tag.arg(data.value("title").constData()).toUtf8());
	} else {
		to_substitute.insert("{{title_part_one}}", "");
	}
	if(data.contains("title2")) {
		to_substitute.insert("{{title_part_two}}", title2_tag.arg(data.value("title2").constData()).toUtf8());
	} else {
		to_substitute.insert("{{title_part_two}}", "");
	}

	return createParamFile("BingReplicationAPI/samples_working/UpdateAds_tmpl.xml", to_substitute, false).toStdString();
}
*/
/*
QByteArray Bing::postFieldsForUpdateGroup(quint64 remote_group_id, const QMap<QByteArray, QByteArray> &data) {
	static QString search_bid_tag = "<SearchBid i:nil=\"false\"><Amount>%1</Amount></SearchBid>";
	static QByteArray skeletonTemplate;
	if(skeletonTemplate.isEmpty()) {
		QString skeletonUrl = "BingReplicationAPI/samples_working/UpdateAdGroups_tmpl.xml";
		QFile inputFile(skeletonUrl);
		inputFile.open(QIODevice::ReadOnly);
		if (!inputFile.isOpen()) {
			superLog("can't load file", Stream::Generic, 0, ALL) << skeletonUrl;
			return "";
		}
		skeletonTemplate = inputFile.readAll();
	}
	QByteArray skeleton = skeletonTemplate;

	change.flag = data.value("status").toUInt();
	skeleton.replace("{{ad_group_id}}", QByteArray::number(remote_group_id));
	skeleton.replace("{{ad_group_status}}", data.value("status") == "1" ? "Active" : "Paused");

	// custom target to substitute
	if(data.contains("c_tare")) {
		double bid = getTare(data);
		change.tare = bid;
		skeleton.replace("{{search_bid}}", search_bid_tag.arg(bid).toUtf8());
	} else {
		skeleton.replace("{{search_bid}}", "");
	}

	if(data.contains("code")) {
		skeleton.replace("{{tracking_infos}}", getTrackingInfos(data).toUtf8());
	} else {
		skeleton.replace("{{tracking_infos}}", "");
	}

	logInBannerChangelog(change);
	skeleton.replace('\n',"");

	return skeleton;

}
*/
/*
std::string Bing::postFieldsForUpdateCampaignLocation(quint64 remote_campaign_id, const QMap<QByteArray, QByteArray> &data) {
	if(!data.contains("nation64")) {
		superLog("no nations info found for campaign", Stream::Generic, 5, ALL) << data.value("id");
		return "";
	}
	QString criterions = getNationCodeList(remote_campaign_id, data.value("nation64"));

	if(criterions.isEmpty()) {
		return "";
	}

	QMap<QString, QByteArray> to_substitute{
		{"{{header_auth}}", getHeader()},
		{"{{campaign_criterions}}", criterions.toUtf8()}
	};

	return createParamFile("BingReplicationAPI/samples_working/AddCampaignCriterions_tmpl.xml", to_substitute, false).toStdString();
}
*/
/*
std::string Bing::postFieldsForUpdateCampaignDevice(quint64 remote_campaign_id, const QMap<QByteArray, QByteArray> &data) {
	if(!data.contains("device_bit")) {
		superLog("no device info found for campaign", Stream::Generic, 5, ALL) << data.value("id");
		return "";
	}
	quint64 deviceMask = data.value("device_bit").toULongLong();
	QString devs = convertDeviceMask(remote_campaign_id, deviceMask);
	if(devs.isEmpty()) {
		return "";
	}

	QMap<QString, QByteArray> to_substitute{
		{"{{header_auth}}", getHeader()},
		{"{{campaign_criterions}}", devs.toUtf8()}
	};

	return createParamFile("BingReplicationAPI/samples_working/AddCampaignCriterions_tmpl.xml", to_substitute, false).toStdString();
}
*/

/*
void Bing::updateBannerHasRemote(quint64 banner_id, quint64 keyword_id) {
	bool failed;

	///@todo maybe do a select fist?
	QString query = "update DSPRemote.banner_has_remote set remoteKeyword = %1 where banner_id = %2 and dsp_id = %3";
	auto res = Options::executeQuery(nullptr,query
									 .arg(keyword_id)
									 .arg(banner_id)
									 .arg(getCode())
									 ,failed);
}
*/

//std::string Bing::postFieldsForInsertAd(const QMap<QByteArray, QByteArray>& data) {
//	QByteArray                url = data.value("url");
//	QMap<QString, QByteArray> to_substitute({{"{{header_auth}}", getHeader()},
//	                                         {"{{ad_group_id}}", data.value("remoteGroupId")},
//	                                         {"{{final_url}}", url.replace('&', "&amp;")},
//	                                         {"{{ad_status}}", data.value("status")},
//	                                         {"{{display_url}}", url.replace('&', "&amp;")},
//	                                         {"{{path_1}}", data.value("path1")},
//	                                         {"{{path_2}}", data.value("path2")},
//	                                         {"{{ad_text}}", data.value("text")},
//	                                         {"{{title_part_1}}", data.value("title")},
//	                                         {"{{title_part_2}}", data.value("title2")},
//											 {"{{title_part_3}}", data.value("title3")}
//											}
//											);
//	return createParamFile("BingReplicationAPI/samples_working/AddAds_tmpl.xml", to_substitute).toStdString();
//}

//std::string Bing::postFieldsForInsertCampaign(const QMap<QByteArray, QByteArray>& data) {
//	static QString languages = "<Languages i:nil=\"false\" xmlns:a1=\"http://schemas.microsoft.com/2003/10/Serialization/Arrays\">%1</Languages >";
//	quint64        status    = data.value("statusng").toULong();
//	bool           enabled   = true; //(status & 1) == 1 && (status & CAMPAIGN_STATUSNG_PAUSED_ALL) == 0;

//	double daily_cap = data.value("cap_daily").toDouble() * spec->conversionRate();

//	QByteArray language = "";
//	QString    langCode = data.value("gm_language").constData();
//	if (spec->useLanguage && langCode.size() > 1) {
//		if (languageCodes.contains(langCode)) {
//			language = QString("<a1:string>%1</a1:string>").arg(languageCodes.value(langCode)).toUtf8();
//		} else {
//			throw QString("language not recognized:" + langCode);
//		}
//	}

//	QMap<QString, QByteArray> to_substitute({{"{{header_auth}}", getHeader()},
//	                                         {"{{account_id}}", spec->accountId},
//	                                         {"{{daily_budget}}", QByteArray::number(daily_cap)},
//	                                         {"{{campaign_description}}", data.value("description")},
//	                                         {"{{campiagn_name}}", data.value("name")},
//	                                         {"{{campaign_status}}", enabled ? "Active" : "Paused"},
//	                                         {"{{languagesBlock}}", language}});

//	return createParamFile("BingReplicationAPI/samples_working/AddCampaigns_tmpl.xml", to_substitute).toStdString();
//}

//bool Bing::insertCampaign(const sqlRow& data) {
//	std::string url = spec->endpoint;

//	std::string params = postFieldsForInsertCampaign(data);

//	CURLpp curlHandler = CURLpp::Builder()
//	                         .set_url(url)
//	                         .set_timeout(10000)
//	                         .set_connect_timeout(2000)
//	                         .add_http_header("Content-Type: text/xml;charset=UTF-8")
//	                         .add_http_header("SOAPAction: AddCampaigns")
//	                         .set_post_fields(params)
//	                         .build();

//	QString response = QString::fromStdString(curlHandler.perform());

//	QString lastError = QString::fromStdString(curlHandler.getLastError());
//	if (!errorCheck(response)) {
//		return false;
//	}

//	//quint64 remote_campaign_id = stoull(json["s:Body"]["AddCampaignsResponse"]["CampaignIds"]["a:long"].asString());
//	//insertCampaignHasRemote(campaign_id, remote_campaign_id, getCode());

//	//updateCampaignLocation(remote_campaign_id, data);
//	//updateCampaignDevice(remote_campaign_id, data);

//	return true;
//}
